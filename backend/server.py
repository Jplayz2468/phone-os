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
            "home": {"name": "Home", "icon": "🏠"},
            "phone": {"name": "Phone", "icon": "📞"},
            "messages": {"name": "Messages", "icon": "💬"},
            "camera": {"name": "Camera", "icon": "📷"},
            "photos": {"name": "Photos", "icon": "🖼️"},
            "settings": {"name": "Settings", "icon": "⚙️"},
            "calculator": {"name": "Calculator", "icon": "🔢"},
            "clock": {"name": "Clock", "icon": "⏰"},
            "weather": {"name": "Weather", "icon": "🌤️"},
            "music": {"name": "Music", "icon": "🎵"},
            "maps": {"name": "Maps", "icon": "🗺️"},
            "browser": {"name": "Browser", "icon": "🌐"}
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
                "temperature": "22°C",
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
    
    def add_notification(self, title, message):
        self.notifications.append({
            "id": len(self.notifications),
            "title": title,
            "message": message,
            "time": datetime.now().strftime("%H:%M")
        })

phone_os = PhoneOS()

async def handler(websocket):
    print("Client connected")
    
    # Send initial status
    await websocket.send(json.dumps(phone_os.get_status()))
    await websocket.send(json.dumps(phone_os.get_apps()))
    
    # Background tasks
    async def status_updater():
        while True:
            await asyncio.sleep(1)
            try:
                await websocket.send(json.dumps(phone_os.get_status()))
            except:
                break
    
    async def random_notifications():
        messages = [
            {"title": "New Message", "message": "Hey, how are you?"},
            {"title": "Weather Alert", "message": "Sunny day ahead!"},
            {"title": "App Update", "message": "Calculator updated"},
            {"title": "Battery", "message": "Battery at 20%"}
        ]
        while True:
            await asyncio.sleep(random.randint(30, 120))
            try:
                msg = random.choice(messages)
                phone_os.add_notification(msg["title"], msg["message"])
                await websocket.send(json.dumps({
                    "type": "notification",
                    "title": msg["title"],
                    "message": msg["message"]
                }))
            except:
                break
    
    # Start background tasks
    asyncio.create_task(status_updater())
    asyncio.create_task(random_notifications())
    
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
                elif data["type"] == "calculator":
                    # Handle calculator operations
                    response = {"type": "calculator_result", "result": eval(data.get("expression", "0"))}
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