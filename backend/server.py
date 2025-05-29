import asyncio
import websockets
import time

async def handler(websocket):
    while True:
        await asyncio.sleep(2)
        await websocket.send("black" if int(time.time()) % 4 < 2 else "darkgreen")

async def main():
    async with websockets.serve(handler, "localhost", 8765):
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())