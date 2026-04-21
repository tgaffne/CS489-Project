import asyncio
import websockets
import json
import random


async def test():
    uri = "ws://localhost:8765"
    lat = 40.4237
    lon = -86.9212
    async with websockets.connect(uri) as ws:
        while True:
            lat += random.randint(-1, 1)/1000
            lon += random.randint(-1, 1)/1000
            data = {
                "lat": lat,
                "lon": lon,
                "hr": random.randint(70, 120)
            }
            await ws.send(json.dumps(data))
            await asyncio.sleep(2)

asyncio.run(test())