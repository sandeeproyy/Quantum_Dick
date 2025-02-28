# Worker Attendance and Tracking System

This system combines ESP32 devices with fingerprint sensors, RFID readers, and GPS modules to track worker attendance and location. Data is stored in Firebase and accessed through a web interface hosted on a Raspberry Pi 4.

## System Components

1. **ESP32 with R307 Fingerprint Sensor and RC522 RFID Module**
   - Handles worker authentication (fingerprint and RFID)
   - Communicates with Firebase for verification and logging

2. **ESP32 with NEO-6M GPS Module (Wearable)**
   - Tracks worker location
   - Uploads GPS coordinates to Firebase
   - Activates when worker clocks in, deactivates when clocked out

3. **Raspberry Pi 4 Web Server**
   - Hosts the web interface
   - Provides admin and worker portals
   - Manages Firebase database operations

## Directory Structure

- `/esp32_auth/` - Code for the ESP32 with fingerprint and RFID modules
- `/esp32_gps/` - Code for the ESP32 wearable with GPS (already implemented)
- `/raspberry_pi/` - Web server and interface code
  - `/static/` - CSS, JavaScript, and images
  - `/templates/` - HTML templates
  - `app.py` - Main Flask application

## Setup Instructions

### ESP32 Authentication Device
1. Connect the R307 fingerprint sensor and RC522 RFID module to the ESP32
2. Flash the ESP32 with the code in `/esp32_auth/`
3. Configure WiFi and Firebase credentials

### Raspberry Pi 4 Web Server
1. Install required packages: `pip install -r requirements.txt`
2. Run the Flask application: `python app.py`
3. Access the web interface at `http://[raspberry_pi_ip]:5000`

## Firebase Database Structure

```
- admin
  - [admin_id]
    - name: "Admin Name"
    - password: "password"
    - workers
      - [worker_id]
        - name: "Worker Name"
        - fingerprint_id: "fingerprint_data"
        - rfid_id: "rfid_data"
        - gps_device_id: "device_id"
        - [date]
          - clock_in: "timestamp"
          - clock_out: "timestamp"
          - breaks: [array_of_break_times]
          - latitude: "current_latitude"
          - longitude: "current_longitude"
          - timestamp: "last_update_time"
```

## Features

### Worker Features
- RFID/Fingerprint Authentication
- Clock In/Out
- Real-time status updates

### Admin Features
- Worker Registration
- Attendance Records Management
- Worker Management
- Report Generation
