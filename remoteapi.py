import threading
import win32file
import pywintypes
import os
import SynapseScape.api.lib.injector as injector
# import SynapseScape.api.RSReflection as RSReflection
import re
import time
from SynapseScape.spatial.world_point import WorldPoint

from SynapseScape.utilities.geometry import Rectangle
from SynapseScape.interaction.remoteio import find_game_client_pid

world_point = re.compile(r"WorldPoint\(x=(\d+), y=(\d+), plane=(\d+)\)")
rectangle = re.compile(r"java.awt.Rectangle\[x=(\d+),y=(\d+),width=(\d+),height=(\d+)\]")
point = re.compile(r"java.awt.Point\[x=(\d+),y=(\d+)\]")
int_array = re.compile(r"int\s*\[\d+\]\s*{\s*((?:\d+\s*,\s*)*\d+)\s*}")

HANDSHAKE_READY = "READY"
HANDSHAKE_GO_AHEAD = "GO_AHEAD"


# TODO: relocate this function
def is_integer(s):
    try:
        int(s)
        return True
    except ValueError:
        return False

def convert(func):
    def wrapper(*args, **kwargs):
        result = func(*args, **kwargs)
        if isinstance(result, str):
            if world_point.match(result):
                return WorldPoint(*map(int, world_point.match(result).groups()))
            elif rectangle.match(result):
                return Rectangle(*map(int, rectangle.match(result).groups()))
            elif point.match(result):
                return [int(point.match(result).group(1)), int(point.match(result).group(2))]
            elif int_array.match(result):
                return [int(i) for i in int_array.match(result).group(1).split(',')]
            elif is_integer(result):
                return int(result)
            else: return result
    return wrapper

class JWrapper:
    def __init__(self, targetClass):
        self.targetClass = targetClass
        self.method_chain = []
        self.api = RemoteAPI()

    def __getattr__(self, methodName):
        return ChainProxy(self, methodName)

    def execute(self):
        query = f"{self.targetClass}." + ".".join(self.method_chain)
        self.method_chain = []
        response = self.api.query(query)
        print(f"Query: {query}\nResponse: {response}")
        return response

class ChainProxy:
    def __init__(self, parent: JWrapper, methodName: str):
        self.parent = parent
        self.methodName = methodName

    def __getattr__(self, nextMethodName):
        self.parent.method_chain.append(self.methodName)
        return ChainProxy(self.parent, nextMethodName)

    def __call__(self, *args):
        args_str = ', '.join(map(str, args))
        method_with_args = f"{self.methodName}({args_str})"
        self.parent.method_chain.append(method_with_args)
        return self.parent

class PipeNotOpenError(Exception):
    """Raised when the pipe is not open for operations."""
    
    def __init__(self, message, data=None):
        self.message = message
        self.data = data  # Store additional data causing the issue

    def __str__(self):
        return f"{self.message}. Data causing the error: {self.data}"

class RemoteAPI:
    _instance = None
    _initialized = False

    def __new__(cls, *args, **kwargs):
        if not cls._instance:
            cls._instance = super(RemoteAPI, cls).__new__(cls, *args, **kwargs)
        return cls._instance

    def __init__(self, encoding='utf-8'):
        if RemoteAPI._initialized:
            return
        try:
            pid = find_game_client_pid()
            print(f"Found game client PID: {pid}")
            if injector.Injector.inject(os.path.join(os.path.dirname(os.path.abspath(__file__)), "lib/JShell.dll"), find_game_client_pid()):
                print("Successfully injected JShell.dll")

            # injector.inject(os.path.join(os.path.dirname(os.path.abspath(__file__)), "RSReflection.dll"), find_game_client_pid())
        except Exception as e:
            print("Error injecting JShell.dll: ", e)
        self.pipe_name = r'\\.\pipe\jshellpipe'
        self.handle = None
        self.encoding = encoding
        self.lock = threading.Lock()  # For thread safety
        self.init_jshell()
        RemoteAPI._initialized = True

    def write_to_pipe(self, message: str) -> bool:
        with self.lock:
            if not self.handle:
                raise PipeNotOpenError("Pipe is not open for writing", message)
            try:
                win32file.WriteFile(self.handle, message.encode(self.encoding))
                return True
            except Exception as e:
                print(f"Error writing to pipe: {e}")
                return False

    def read_from_pipe(self, buffer_size=32768) -> str:
        with self.lock:
            try:
                result, data = win32file.ReadFile(self.handle, buffer_size)
                return data.decode(self.encoding)
            except Exception as e:
                print(f"Error reading from pipe: {e}")
                return None

    def __enter__(self):
        with self.lock:
            retries = 20  # or however many retries you deem appropriate

            for _ in range(retries):
                try:
                    self.handle = win32file.CreateFile(
                        self.pipe_name,
                        win32file.GENERIC_READ | win32file.GENERIC_WRITE,
                        0,
                        None,
                        win32file.OPEN_EXISTING,
                        0,
                        None
                    )
                    return self  # connection succeeded, break out of the loop
                except pywintypes.error as e:
                    if "All pipe instances are busy" in str(e):
                        time.sleep(0.1)
                        continue  # retry immediately
                    else:
                        print(str(e))
                        raise PipeNotOpenError(f"Could not open pipe", self.pipe_name) from e

            # If you reach here, all retries failed
            raise PipeNotOpenError(f"Could not open pipe after {retries} retries", self.pipe_name)


    def __exit__(self, exc_type, exc_value, traceback):
        with self.lock:
            if self.handle:
                try:
                    win32file.CloseHandle(self.handle)
                    self.handle = None
                except Exception as e:
                    print(f"Error closing pipe: {e}")

    @convert
    def query(self, script: str):
        assert isinstance(script, str)
        if not script[-1] == ';':
            script += ';'
        with self:
            # Start the handshake process
            self.write_to_pipe(HANDSHAKE_READY)
            handshake_response = self.read_from_pipe()
            if handshake_response != HANDSHAKE_GO_AHEAD:
                raise Exception("Handshake failed. Expected GO_AHEAD but received: " + handshake_response)
            # Handshake successful, send actual data
            self.write_to_pipe(script)
            response = self.read_from_pipe()
        return response

    
    def init_jshell(self):
        self.query("import java.awt.Rectangle;")
        self.query("import java.awt.Point;")
        self.query("import java.awt.Polygon;")
        self.query("import java.awt.Canvas;")
        self.query("import net.runelite.api.coords.LocalPoint;")
        self.query("import net.runelite.api.Perspective;")
        self.query("import net.runelite.api.coords.WorldPoint;")
        self.query("import net.runelite.api.Client;")
        self.query("import net.runelite.api.Scene;")
        self.query("import net.runelite.api.Tile;")
        self.query("import net.runelite.api.coords.*;")
        self.query("import net.runelite.api.*;")
        self.query("import java.awt.Canvas;")
        self.query("import java.lang.reflect.*;")
        self.query("import net.runelite.api.TileObject;")
        self.query("import net.runelite.api.GameObject;")
        self.query("import net.runelite.api.WallObject;")
        self.query("import net.runelite.api.DecorativeObject;")
        self.query("import net.runelite.api.GroundObject;")
        self.query("import java.util.*;")
        self.query("import java.util.stream.Collectors;")
        self.query("import java.util.stream.Stream;")
        self.query("import net.runelite.api.InventoryID;")
        self.query("import net.runelite.api.ItemContainer;")
        self.query("import net.runelite.api.Item;")
        self.query("import net.runelite.api.Item;")
        self.query("import net.runelite.api.widgets.WidgetInfo;")
        self.query("import net.runelite.api.widgets.Widget;")
        self.query("import java.lang.Exception;")
        self.query("import java.util.List;")
        self.query("import java.util.ArrayList;")
        self.query("import java.util.HashSet;")
        self.query("import java.util.ArrayDeque;")
        self.query("import java.util.Collections;")

        self.query('''
            public class Node {
                WorldPoint data;
                Node previous;

                Node(WorldPoint data) {
                    this.data = data;
                }

                Node() {
                    this.data = null;
                    this.previous = null;
                }

                Node(WorldPoint data, Node previous) {
                    this.data = data;
                    this.previous = previous;
                }

                public WorldPoint getData() {
                    return data;
                }

                public Node getPrevious() {
                    return previous;
                }

                public void setNode(WorldPoint data, Node previous) {
                    this.data = data;
                    this.previous = previous;
                }
            }''')

        self.query('''
            public static List<WorldPoint> findPath(Client client, WorldPoint p) {
                long start = System.currentTimeMillis();
                WorldPoint starting = client.getLocalPlayer().getWorldLocation();
                HashSet<WorldPoint> visited = new HashSet<>();
                ArrayDeque<Node> queue = new ArrayDeque<Node>();
                queue.add(new Node(starting));
                while (!queue.isEmpty()) {
                    Node current = queue.poll();
                    WorldPoint currentData = current.getData();
                    if (currentData.equals(p)) {
                        List<WorldPoint> ret = new ArrayList<>();
                        while (current != null) {
                            ret.add(current.getData());
                            current = current.getPrevious();
                        }
                        Collections.reverse(ret);
                        ret.remove(0);
                        System.out.println("Path took " + (System.currentTimeMillis() - start) + "ms");
                        return ret;
                    }
                    //west
                    if (west(currentData) && visited.add(currentData.dx(-1))) {
                        queue.add(new Node(currentData.dx(-1), current));
                    }
                    //east
                    if (east(currentData) && visited.add(currentData.dx(1))) {
                        queue.add(new Node(currentData.dx(1), current));
                    }
                    //south
                    if (south(currentData) && visited.add(currentData.dy(-1))) {
                        queue.add(new Node(currentData.dy(-1), current));
                    }
                    //north
                    if (north(currentData) && visited.add(currentData.dy(1))) {
                        queue.add(new Node(currentData.dy(1), current));
                    }
                }
                return null;
            }''')

        self.query('''
            public static Rectangle getTileClickbox(Client client, WorldPoint tile) {
                LocalPoint lp = LocalPoint.fromWorld(client, tile);
                Polygon p = null;
                try {
                   p = Perspective.getCanvasTilePoly(client, lp);
                }
                catch (Exception e) {
                    return null;
                }
                
                if (p == null) {
                    return null;
                }
                if (p.npoints == 0) {
                    return null;
                }

                return p.getBounds();
            }''')

        self.query('''
            public static String findTileObject(Client client, int id) {
                Scene scene = client.getScene();
                Tile[][][] tiles = scene.getTiles();
                Tile[][] tile = tiles[client.getPlane()];
                List foundLocations = new ArrayList<WorldPoint>();
                for (int i=0; i < tile.length; i++) {
                    for (int j=0; j < tile[i].length; j++) {
                            if (tile[i][j] != null) {
                                for (GameObject gameObject : tile[i][j].getGameObjects()) {
                                    if (gameObject != null && gameObject.getId() == id) {
                                        foundLocations.add(gameObject.getWorldLocation());
                                    }
                                }

                                WallObject wallObject = tile[i][j].getWallObject();
                                if (wallObject != null && wallObject.getId() == id) {
                                    foundLocations.add(wallObject.getWorldLocation());
                                }

                                DecorativeObject decorativeObject = tile[i][j].getDecorativeObject();
                                if (decorativeObject != null && decorativeObject.getId() == id) {
                                    foundLocations.add(decorativeObject.getWorldLocation());
                                }

                                GroundObject groundObject = tile[i][j].getGroundObject();
                                if (groundObject != null && groundObject.getId() == id) {
                                    foundLocations.add(groundObject.getWorldLocation());
                                }
                            }
                        }
                    }
                if (foundLocations.size() > 0) {
                    return foundLocations.toString();
                }
                else {
                    return "null";
                }
            }''')


if __name__ == '__main__':
    client = JWrapper("client")
    api = RemoteAPI()
    print(api.query('Canvas.class.getMethod("getLocationOnScreen").invoke(client.getCanvas())'))
    #print(api.query('Canvas.class.getMethods()'))

    print(api.query('getTileClickbox(client, new WorldPoint(1942, 4967, 0));'))
    print(client.getGameState().execute())
    #print(client.getClientThread().execute())
    #print(client.getLocalPlayer().getWorldLocation().execute())
    #print(client.isOnLoginScreen().execute())
    #print(client.isClientThread().execute())
    #print(client.getLocation().execute())
    #print(client.getCanvas().execute())