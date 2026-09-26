"""launch.py - start the PC game fully detached from this console.

The game inherits the parent's stdout/stderr pipes; when the piped parent is
killed, Windows tears down the child too. DETACHED_PROCESS + DEVNULL handles
sever that link so the game survives the tool call ending.
"""
import os, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
GAME_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "game_pc"))
EXE = os.path.join(GAME_DIR, "EchidnaWarsDX.exe")

DETACHED_PROCESS = 0x00000008
CREATE_NEW_PROCESS_GROUP = 0x00000200

flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
p = subprocess.Popen([EXE], cwd=GAME_DIR,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                     stdin=subprocess.DEVNULL,
                     creationflags=flags, close_fds=True)
print("launched pid", p.pid)
