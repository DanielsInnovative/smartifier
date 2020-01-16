# smartifier

Smartifier is a system of ESP32 nodes used to "smartify" existing appliances and devices. The fundamental smartifier code base is:
- ESP32 (I use devkit)
- BLE beacon listener/republisher to mqtt
- NTP
- Wifi scanning (stretch goal)

The core node is called a gateway and only does above. Any specialized smartifier node is ALSO a gateway, but incudes logic on top of that to perform smartification for a target appliance.

Planned specialty smartifiers include:
- laundry (washer & dryer, add vibration sensors, temp/humidity sensor, current sensor)
- water heater (two temperature sensors and a current sensor)
- door bell (add reed switch)
- garage (reed switches for doors, relay to fire open/close)

