import asyncio
import websockets
import json
import time
import random
from datetime import datetime

class PhoneOS:
    def __init__(self):
        self.current_app = "home"
        self.notifications = []
        self.battery = 85
        self.wifi_strength = 3
        self.brightness = 80
        self.volume = 60
        self.apps = {
            "home": {"name": "Home", "icon": "H"},
            "phone": {"name": "Phone", "icon": "P"},
            "messages": {"name": "Messages", "icon": "M"},
            "camera": {"name": "Camera", "icon": "C"},
            "photos": {"name": "Photos", "icon": "I"},
            "settings": {"name": "Settings", "icon": "S"},
            "calculator": {"name": "Calculator", "icon": "#"},
            "clock": {"name": "Clock", "icon": "T"},
            "weather": {"name": "Weather", "icon": "W"},
            "music": {"name": "Music", "icon": "♪"},
            "maps": {"name": "Maps", "icon": "MAP"},
            "browser": {"name": "Browser", "icon": "B"}
        }
        
    def get_status(self):
        now = datetime.now()
        return {
            "type": "status",
            "time": now.strftime("%H:%M"),
            "date": now.strftime("%A, %B %d"),
            "battery": self.battery,
            "wifi": self.wifi_strength,
            "current_app": self.current_app,
            "notifications": len(self.notifications),
            "brightness": self.brightness,
            "volume": self.volume
        }
    
    def get_apps(self):
        return {
            "type": "apps",
            "apps": self.apps
        }
    
    def open_app(self, app_id):
        if app_id in self.apps:
            self.current_app = app_id
            return {
                "type": "app_opened",
                "app": app_id,
                "app_data": self.get_app_data(app_id)
            }
        return {"type": "error", "message": "App not found"}
    
    def get_app_data(self, app_id):
        if app_id == "calculator":
            return {"display": "0", "operation": None}
        elif app_id == "weather":
            return {
                "temperature": "22C",
                "condition": "Sunny",
                "humidity": "65%",
                "wind": "5 km/h"
            }
        elif app_id == "settings":
            return {
                "brightness": self.brightness,
                "volume": self.volume,
                "wifi": self.wifi_strength > 0
            }
        return {}

phone_os = PhoneOS()

async def handler(websocket):
    print("Client connected")
    
    await websocket.send(json.dumps(phone_os.get_status()))
    await websocket.send(json.dumps(phone_os.get_apps()))
    
    async def status_updater():
        while True:
            await asyncio.sleep(5)
            try:
                await websocket.send(json.dumps(phone_os.get_status()))
            except:
                break
    
    asyncio.create_task(status_updater())
    
    try:
        async for message in websocket:
            try:
                data = json.loads(message)
                response = None
                
                if data["type"] == "open_app":
                    response = phone_os.open_app(data["app_id"])
                elif data["type"] == "home":
                    phone_os.current_app = "home"
                    response = {"type": "app_closed"}
                elif data["type"] == "get_apps":
                    response = phone_os.get_apps()
                elif data["type"] == "settings_change":
                    if "brightness" in data:
                        phone_os.brightness = data["brightness"]
                    if "volume" in data:
                        phone_os.volume = data["volume"]
                    response = {"type": "settings_updated"}
                
                if response:
                    await websocket.send(json.dumps(response))
                    
            except json.JSONDecodeError:
                print("Invalid JSON received")
            except Exception as e:
                print(f"Error processing message: {e}")
                
    except websockets.exceptions.ConnectionClosed:
        print("Client disconnected")

async def main():
    print("Starting Phone OS WebSocket server on localhost:8765")
    async with websockets.serve(handler, "localhost", 8765):
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())