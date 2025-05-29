import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:web_socket_channel/web_socket_channel.dart';
import 'dart:convert';
import 'dart:async';

void main() {
  runApp(PhoneOSApp());
}

class PhoneOSApp extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersive);
    return MaterialApp(
      home: PhoneOS(),
      debugShowCheckedModeBanner: false,
    );
  }
}

class PhoneOS extends StatefulWidget {
  @override
  _PhoneOSState createState() => _PhoneOSState();
}

class _PhoneOSState extends State<PhoneOS> with TickerProviderStateMixin {
  late WebSocketChannel channel;
  late AnimationController _slideController;
  late Animation<Offset> _slideAnimation;
  
  String currentApp = 'home';
  Map<String, dynamic> apps = {};
  String currentTime = '';
  String currentDate = '';
  int battery = 85;
  
  @override
  void initState() {
    super.initState();
    
    _slideController = AnimationController(
      duration: Duration(milliseconds: 300),
      vsync: this,
    );
    
    _slideAnimation = Tween<Offset>(
      begin: Offset.zero,
      end: Offset(-1.0, 0.0),
    ).animate(CurvedAnimation(
      parent: _slideController,
      curve: Curves.easeOutCubic,
    ));
    
    connectWebSocket();
    updateTime();
    Timer.periodic(Duration(seconds: 1), (timer) => updateTime());
  }
  
  void connectWebSocket() {
    try {
      channel = WebSocketChannel.connect(Uri.parse('ws://localhost:8765'));
      channel.stream.listen((data) {
        final message = json.decode(data);
        setState(() {
          if (message['type'] == 'status') {
            currentTime = message['time'];
            currentDate = message['date'];
            battery = message['battery'];
          } else if (message['type'] == 'apps') {
            apps = message['apps'];
          }
        });
      });
    } catch (e) {
      print('WebSocket connection failed: $e');
    }
  }
  
  void updateTime() {
    final now = DateTime.now();
    setState(() {
      currentTime = '${now.hour.toString().padLeft(2, '0')}:${now.minute.toString().padLeft(2, '0')}';
      final weekdays = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday'];
      final months = ['January', 'February', 'March', 'April', 'May', 'June', 
                     'July', 'August', 'September', 'October', 'November', 'December'];
      currentDate = '${weekdays[now.weekday - 1]}, ${months[now.month - 1]} ${now.day}';
    });
  }
  
  void openApp(String appId) async {
    if (_slideController.isAnimating) return;
    
    setState(() => currentApp = appId);
    await _slideController.forward();
  }
  
  void goHome() async {
    if (_slideController.isAnimating) return;
    
    await _slideController.reverse();
    setState(() => currentApp = 'home');
  }
  
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Container(
        decoration: BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
            colors: [Color(0xFF2c3e50), Color(0xFF3498db)],
          ),
        ),
        child: Stack(
          children: [
            // Home Screen
            SlideTransition(
              position: _slideAnimation,
              child: currentApp == 'home' ? _buildHomeScreen() : Container(),
            ),
            
            // App Screen
            if (currentApp != 'home')
              SlideTransition(
                position: Tween<Offset>(
                  begin: Offset(1.0, 0.0),
                  end: Offset.zero,
                ).animate(_slideController),
                child: _buildAppScreen(),
              ),
            
            // Status Bar
            _buildStatusBar(),
          ],
        ),
      ),
    );
  }
  
  Widget _buildStatusBar() {
    return Container(
      height: 100,
      padding: EdgeInsets.symmetric(horizontal: 40),
      decoration: BoxDecoration(
        color: Colors.black.withOpacity(0.5),
      ),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(
            currentTime,
            style: TextStyle(color: Colors.white, fontSize: 28, fontWeight: FontWeight.bold),
          ),
          Row(
            children: [
              Text('WIFI', style: TextStyle(color: Colors.white, fontSize: 24)),
              SizedBox(width: 20),
              Text('BAT $battery%', style: TextStyle(color: Colors.white, fontSize: 24)),
            ],
          ),
        ],
      ),
    );
  }
  
  Widget _buildHomeScreen() {
    return Padding(
      padding: EdgeInsets.only(top: 100),
      child: Column(
        children: [
          // Clock Widget
          Container(
            padding: EdgeInsets.symmetric(vertical: 80),
            child: Column(
              children: [
                Text(
                  currentTime,
                  style: TextStyle(
                    color: Colors.white,
                    fontSize: 120,
                    fontWeight: FontWeight.w300,
                  ),
                ),
                SizedBox(height: 20),
                Text(
                  currentDate,
                  style: TextStyle(
                    color: Colors.white.withOpacity(0.9),
                    fontSize: 40,
                    fontWeight: FontWeight.w300,
                  ),
                ),
              ],
            ),
          ),
          
          // App Grid
          Expanded(
            child: GridView.builder(
              padding: EdgeInsets.all(40),
              gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                crossAxisCount: 4,
                crossAxisSpacing: 40,
                mainAxisSpacing: 40,
                childAspectRatio: 1.0,
              ),
              itemCount: apps.length - 1, // Exclude home
              itemBuilder: (context, index) {
                final appIds = apps.keys.where((key) => key != 'home').toList();
                final appId = appIds[index];
                final app = apps[appId];
                
                return GestureDetector(
                  onTap: () => openApp(appId),
                  child: Container(
                    decoration: BoxDecoration(
                      color: Colors.white.withOpacity(0.2),
                      borderRadius: BorderRadius.circular(20),
                      border: Border.all(color: Colors.white.withOpacity(0.3), width: 2),
                    ),
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        Text(
                          app['icon'],
                          style: TextStyle(fontSize: 60, color: Colors.white),
                        ),
                        SizedBox(height: 15),
                        Text(
                          app['name'],
                          style: TextStyle(
                            color: Colors.white,
                            fontSize: 24,
                            fontWeight: FontWeight.bold,
                          ),
                          textAlign: TextAlign.center,
                        ),
                      ],
                    ),
                  ),
                );
              },
            ),
          ),
        ],
      ),
    );
  }
  
  Widget _buildAppScreen() {
    return Container(
      color: Colors.black.withOpacity(0.95),
      child: Column(
        children: [
          // App Header
          Container(
            height: 220,
            padding: EdgeInsets.only(left: 40, right: 40, top: 100),
            child: Row(
              children: [
                GestureDetector(
                  onTap: goHome,
                  child: Container(
                    width: 80,
                    height: 80,
                    decoration: BoxDecoration(
                      color: Colors.white.withOpacity(0.3),
                      shape: BoxShape.circle,
                    ),
                    child: Icon(Icons.arrow_back, color: Colors.white, size: 40),
                  ),
                ),
                SizedBox(width: 30),
                Text(
                  apps[currentApp]?['name'] ?? 'App',
                  style: TextStyle(
                    color: Colors.white,
                    fontSize: 36,
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ],
            ),
          ),
          
          // App Content
          Expanded(
            child: _buildAppContent(),
          ),
        ],
      ),
    );
  }
  
  Widget _buildAppContent() {
    if (currentApp == 'calculator') {
      return _buildCalculator();
    } else if (currentApp == 'settings') {
      return _buildSettings();
    } else {
      return Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Text('APP', style: TextStyle(fontSize: 120, color: Colors.white)),
            SizedBox(height: 40),
            Text('Coming Soon', style: TextStyle(fontSize: 48, color: Colors.white)),
            SizedBox(height: 20),
            Text('This app is under development', 
                 style: TextStyle(fontSize: 32, color: Colors.white.withOpacity(0.7))),
          ],
        ),
      );
    }
  }
  
  Widget _buildCalculator() {
    return Container(
      padding: EdgeInsets.all(40),
      child: Column(
        children: [
          Container(
            height: 150,
            width: double.infinity,
            decoration: BoxDecoration(
              color: Colors.white.withOpacity(0.2),
              borderRadius: BorderRadius.circular(10),
            ),
            alignment: Alignment.centerRight,
            padding: EdgeInsets.symmetric(horizontal: 30),
            child: Text('0', style: TextStyle(fontSize: 60, color: Colors.white, fontWeight: FontWeight.bold)),
          ),
          SizedBox(height: 30),
          Expanded(
            child: GridView.count(
              crossAxisCount: 4,
              crossAxisSpacing: 20,
              mainAxisSpacing: 20,
              children: [
                'C', '+/-', '%', '/',
                '7', '8', '9', '*',
                '4', '5', '6', '-',
                '1', '2', '3', '+',
                '0', '0', '.', '=',
              ].map((text) => GestureDetector(
                onTap: () {},
                child: Container(
                  decoration: BoxDecoration(
                    color: Colors.white.withOpacity(0.2),
                    borderRadius: BorderRadius.circular(10),
                    border: Border.all(color: Colors.white.withOpacity(0.3), width: 2),
                  ),
                  child: Center(
                    child: Text(text, style: TextStyle(fontSize: 48, color: Colors.white, fontWeight: FontWeight.bold)),
                  ),
                ),
              )).toList(),
            ),
          ),
        ],
      ),
    );
  }
  
  Widget _buildSettings() {
    return Container(
      padding: EdgeInsets.all(40),
      child: Column(
        children: [
          _buildSettingsItem('Brightness', Slider(value: 0.8, onChanged: (v) {})),
          _buildSettingsItem('Volume', Slider(value: 0.6, onChanged: (v) {})),
          _buildSettingsItem('WiFi', Text('Connected', style: TextStyle(color: Colors.white, fontSize: 24))),
          _buildSettingsItem('Battery', Text('$battery%', style: TextStyle(color: Colors.white, fontSize: 24))),
        ],
      ),
    );
  }
  
  Widget _buildSettingsItem(String label, Widget control) {
    return Container(
      margin: EdgeInsets.only(bottom: 20),
      padding: EdgeInsets.all(30),
      decoration: BoxDecoration(
        color: Colors.white.withOpacity(0.2),
        borderRadius: BorderRadius.circular(10),
      ),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(label, style: TextStyle(fontSize: 32, color: Colors.white, fontWeight: FontWeight.bold)),
          control,
        ],
      ),
    );
  }
  
  @override
  void dispose() {
    _slideController.dispose();
    channel.sink.close();
    super.dispose();
  }
}