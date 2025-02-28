"""
Worker Attendance and Tracking System - Web Server
Runs on Raspberry Pi 4 and provides web interface for the system
"""

import os
import json
import csv
import datetime
from flask import Flask, render_template, request, jsonify, redirect, url_for, send_file
import firebase_admin
from firebase_admin import credentials, db
import pyrebase
import pandas as pd
from io import StringIO

app = Flask(__name__)

# Firebase Admin SDK initialization (for server-side operations)
cred = credentials.Certificate("firebase-adminsdk.json")
firebase_admin.initialize_app(cred, {
    'databaseURL': 'https://worknest01-default-rtdb.asia-southeast1.firebasedatabase.app'
})

# Pyrebase initialization (for client-side operations)
firebase_config = {
    "apiKey": "AIzaSyBIE39DJ_fZNnS5mucUDtCBw-qrXevSCOI",
    "authDomain": "worknest01.firebaseapp.com",
    "databaseURL": "https://worknest01-default-rtdb.asia-southeast1.firebasedatabase.app",
    "projectId": "worknest01",
    "storageBucket": "worknest01.firebasestorage.app",
    "messagingSenderId": "497079547008",
    "appId": "1:497079547008:web:fea7450ba0f40ba2e1a265",
    "measurementId": "G-3EX2Q47B70"
}

firebase = pyrebase.initialize_app(firebase_config)
auth = firebase.auth()
database = firebase.database()

# Global variables
current_admin = None

# Helper functions
def get_current_date():
    """Get current date in YYYY-MM-DD format"""
    return datetime.datetime.now().strftime("%Y-%m-%d")

def get_current_time():
    """Get current time in HH:MM:SS format"""
    return datetime.datetime.now().strftime("%H:%M:%S")

def generate_worker_id():
    """Generate a unique worker ID"""
    # Get all worker IDs from all admins
    all_worker_ids = []
    admins_ref = db.reference('/admin')
    admins = admins_ref.get()
    
    if admins:
        for admin_id, admin_data in admins.items():
            if 'workers' in admin_data:
                all_worker_ids.extend(list(admin_data['workers'].keys()))
    
    # Find the highest worker ID and increment by 1
    if all_worker_ids:
        highest_id = max([int(wid) for wid in all_worker_ids])
        return str(highest_id + 1)
    else:
        return "1000"  # Start with 1000 if no workers exist

@app.route('/')
def index():
    """Home page / Login screen"""
    return render_template('index.html')

@app.route('/login', methods=['POST'])
def login():
    """Handle admin login"""
    username = request.form.get('username')
    password = request.form.get('password')
    
    # Check if admin exists in Firebase
    admins_ref = db.reference('/admin')
    admins = admins_ref.get()
    
    if admins:
        for admin_id, admin_data in admins.items():
            if admin_data.get('name') == username and admin_data.get('password') == password:
                global current_admin
                current_admin = {
                    'id': admin_id,
                    'name': username
                }
                return redirect(url_for('admin_dashboard'))
    
    # If no match found
    return render_template('index.html', error="Invalid username or password")

@app.route('/worker_auth', methods=['POST'])
def worker_auth():
    """Handle worker authentication via RFID or fingerprint"""
    auth_type = request.form.get('auth_type')
    auth_value = request.form.get('auth_value')
    
    worker_id = None
    worker_data = None
    admin_id = None
    
    # Search for worker in all admins
    admins_ref = db.reference('/admin')
    admins = admins_ref.get()
    
    if admins:
        for a_id, admin_data in admins.items():
            if 'workers' in admin_data:
                for w_id, w_data in admin_data['workers'].items():
                    if (auth_type == 'rfid' and w_data.get('rfid_id') == auth_value) or \
                       (auth_type == 'fingerprint' and w_data.get('fingerprint_id') == auth_value):
                        worker_id = w_id
                        worker_data = w_data
                        admin_id = a_id
                        break
                
                if worker_id:
                    break
    
    if worker_id:
        # Check if worker is an admin
        if worker_data.get('is_admin', False):
            global current_admin
            current_admin = {
                'id': admin_id,
                'name': worker_data.get('name', 'Admin')
            }
            return redirect(url_for('admin_dashboard'))
        else:
            # Regular worker, redirect to clock in/out page
            return redirect(url_for('worker_dashboard', worker_id=worker_id, admin_id=admin_id))
    
    # If no match found
    return render_template('index.html', error="Worker not found")

@app.route('/worker/<admin_id>/<worker_id>')
def worker_dashboard(admin_id, worker_id):
    """Worker dashboard for clock in/out"""
    # Get worker data
    worker_ref = db.reference(f'/admin/{admin_id}/workers/{worker_id}')
    worker_data = worker_ref.get()
    
    if not worker_data:
        return redirect(url_for('index'))
    
    # Get worker's last action (clock in/out)
    current_date = get_current_date()
    today_ref = worker_ref.child(current_date)
    today_data = today_ref.get()
    
    last_action = "out"  # Default to out
    if today_data:
        if 'clock_in' in today_data and 'clock_out' not in today_data:
            last_action = "in"
        elif 'clock_in' in today_data and 'clock_out' in today_data:
            last_action = "out"
    
    return render_template('worker_dashboard.html', 
                          worker=worker_data, 
                          worker_id=worker_id,
                          admin_id=admin_id,
                          last_action=last_action)

@app.route('/clock_action', methods=['POST'])
def clock_action():
    """Handle clock in/out action"""
    admin_id = request.form.get('admin_id')
    worker_id = request.form.get('worker_id')
    action = request.form.get('action')  # 'in' or 'out'
    
    current_date = get_current_date()
    current_time = get_current_time()
    
    # Update Firebase
    worker_ref = db.reference(f'/admin/{admin_id}/workers/{worker_id}/{current_date}')
    
    if action == 'in':
        worker_ref.update({
            'clock_in': current_time
        })
        
        # Activate GPS tracking for this worker
        gps_device_ref = db.reference(f'/admin/{admin_id}/workers/{worker_id}/gps_device_id')
        gps_device_id = gps_device_ref.get()
        
        if gps_device_id:
            db.reference(f'/gps_devices/{gps_device_id}').update({
                'active': True
            })
    else:  # action == 'out'
        worker_ref.update({
            'clock_out': current_time
        })
        
        # Deactivate GPS tracking for this worker
        gps_device_ref = db.reference(f'/admin/{admin_id}/workers/{worker_id}/gps_device_id')
        gps_device_id = gps_device_ref.get()
        
        if gps_device_id:
            db.reference(f'/gps_devices/{gps_device_id}').update({
                'active': False
            })
    
    return redirect(url_for('worker_dashboard', admin_id=admin_id, worker_id=worker_id))

@app.route('/admin/dashboard')
def admin_dashboard():
    """Admin dashboard"""
    if not current_admin:
        return redirect(url_for('index'))
    
    # Get all workers for this admin
    workers_ref = db.reference(f'/admin/{current_admin["id"]}/workers')
    workers_data = workers_ref.get() or {}
    
    # Get today's attendance statistics
    current_date = get_current_date()
    total_workers = len(workers_data)
    clocked_in = 0
    clocked_out = 0
    
    for worker_id, worker_data in workers_data.items():
        if current_date in worker_data:
            if 'clock_in' in worker_data[current_date]:
                if 'clock_out' not in worker_data[current_date]:
                    clocked_in += 1
                else:
                    clocked_out += 1
    
    return render_template('admin_dashboard.html', 
                          admin=current_admin,
                          total_workers=total_workers,
                          clocked_in=clocked_in,
                          clocked_out=clocked_out)

@app.route('/admin/workers')
def admin_workers():
    """Admin worker management page"""
    if not current_admin:
        return redirect(url_for('index'))
    
    # Get all workers for this admin
    workers_ref = db.reference(f'/admin/{current_admin["id"]}/workers')
    workers_data = workers_ref.get() or {}
    
    return render_template('admin_workers.html', 
                          admin=current_admin,
                          workers=workers_data)

@app.route('/admin/attendance')
def admin_attendance():
    """Admin attendance logs page"""
    if not current_admin:
        return redirect(url_for('index'))
    
    # Get date filter (default to today)
    filter_date = request.args.get('date', get_current_date())
    
    # Get all workers for this admin
    workers_ref = db.reference(f'/admin/{current_admin["id"]}/workers')
    workers_data = workers_ref.get() or {}
    
    # Prepare attendance data
    attendance_data = []
    
    for worker_id, worker_data in workers_data.items():
        if filter_date in worker_data:
            attendance = {
                'worker_id': worker_id,
                'name': worker_data.get('name', 'Unknown'),
                'clock_in': worker_data[filter_date].get('clock_in', ''),
                'clock_out': worker_data[filter_date].get('clock_out', ''),
                'status': 'Present'
            }
            
            # Calculate hours worked if both clock in and out are available
            if attendance['clock_in'] and attendance['clock_out']:
                try:
                    time_in = datetime.datetime.strptime(attendance['clock_in'], "%H:%M:%S")
                    time_out = datetime.datetime.strptime(attendance['clock_out'], "%H:%M:%S")
                    duration = time_out - time_in
                    hours_worked = duration.total_seconds() / 3600
                    attendance['hours_worked'] = f"{hours_worked:.2f}"
                except:
                    attendance['hours_worked'] = "Error"
            else:
                attendance['hours_worked'] = "In progress" if attendance['clock_in'] else "N/A"
            
            attendance_data.append(attendance)
    
    return render_template('admin_attendance.html', 
                          admin=current_admin,
                          attendance=attendance_data,
                          filter_date=filter_date)

@app.route('/admin/register', methods=['GET', 'POST'])
def admin_register_worker():
    """Admin worker registration page"""
    if not current_admin:
        return redirect(url_for('index'))
    
    if request.method == 'POST':
        # Get form data
        name = request.form.get('name')
        rfid_id = request.form.get('rfid_id')
        fingerprint_id = request.form.get('fingerprint_id')
        gps_device_id = request.form.get('gps_device_id')
        is_admin = request.form.get('is_admin') == 'on'
        
        # Generate worker ID
        worker_id = generate_worker_id()
        
        # Create worker data
        worker_data = {
            'name': name,
            'rfid_id': rfid_id,
            'fingerprint_id': fingerprint_id,
            'gps_device_id': gps_device_id,
            'is_admin': is_admin
        }
        
        # Save to Firebase
        worker_ref = db.reference(f'/admin/{current_admin["id"]}/workers/{worker_id}')
        worker_ref.set(worker_data)
        
        return redirect(url_for('admin_workers'))
    
    return render_template('admin_register.html', admin=current_admin)

@app.route('/admin/edit_worker/<worker_id>', methods=['GET', 'POST'])
def admin_edit_worker(worker_id):
    """Admin edit worker page"""
    if not current_admin:
        return redirect(url_for('index'))
    
    worker_ref = db.reference(f'/admin/{current_admin["id"]}/workers/{worker_id}')
    
    if request.method == 'POST':
        # Get form data
        name = request.form.get('name')
        rfid_id = request.form.get('rfid_id')
        fingerprint_id = request.form.get('fingerprint_id')
        gps_device_id = request.form.get('gps_device_id')
        is_admin = request.form.get('is_admin') == 'on'
        
        # Update worker data
        worker_data = {
            'name': name,
            'rfid_id': rfid_id,
            'fingerprint_id': fingerprint_id,
            'gps_device_id': gps_device_id,
            'is_admin': is_admin
        }
        
        # Save to Firebase
        worker_ref.update(worker_data)
        
        return redirect(url_for('admin_workers'))
    
    # Get worker data for form
    worker_data = worker_ref.get()
    
    if not worker_data:
        return redirect(url_for('admin_workers'))
    
    return render_template('admin_edit_worker.html', 
                          admin=current_admin,
                          worker=worker_data,
                          worker_id=worker_id)

@app.route('/admin/delete_worker/<worker_id>', methods=['POST'])
def admin_delete_worker(worker_id):
    """Delete worker"""
    if not current_admin:
        return redirect(url_for('index'))
    
    worker_ref = db.reference(f'/admin/{current_admin["id"]}/workers/{worker_id}')
    worker_ref.delete()
    
    return redirect(url_for('admin_workers'))

@app.route('/admin/export_attendance', methods=['POST'])
def admin_export_attendance():
    """Export attendance data as CSV"""
    if not current_admin:
        return redirect(url_for('index'))
    
    # Get date filter
    filter_date = request.form.get('date', get_current_date())
    
    # Get all workers for this admin
    workers_ref = db.reference(f'/admin/{current_admin["id"]}/workers')
    workers_data = workers_ref.get() or {}
    
    # Prepare attendance data
    attendance_data = []
    
    for worker_id, worker_data in workers_data.items():
        if filter_date in worker_data:
            attendance = {
                'Worker ID': worker_id,
                'Name': worker_data.get('name', 'Unknown'),
                'Clock In': worker_data[filter_date].get('clock_in', ''),
                'Clock Out': worker_data[filter_date].get('clock_out', ''),
                'Status': 'Present'
            }
            
            # Calculate hours worked if both clock in and out are available
            if attendance['Clock In'] and attendance['Clock Out']:
                try:
                    time_in = datetime.datetime.strptime(attendance['Clock In'], "%H:%M:%S")
                    time_out = datetime.datetime.strptime(attendance['Clock Out'], "%H:%M:%S")
                    duration = time_out - time_in
                    hours_worked = duration.total_seconds() / 3600
                    attendance['Hours Worked'] = f"{hours_worked:.2f}"
                except:
                    attendance['Hours Worked'] = "Error"
            else:
                attendance['Hours Worked'] = "In progress" if attendance['Clock In'] else "N/A"
            
            attendance_data.append(attendance)
    
    # Create CSV
    if attendance_data:
        df = pd.DataFrame(attendance_data)
        csv_data = df.to_csv(index=False)
        
        # Create a StringIO object
        csv_io = StringIO()
        csv_io.write(csv_data)
        csv_io.seek(0)
        
        # Create a response with the CSV data
        return send_file(
            csv_io,
            mimetype='text/csv',
            as_attachment=True,
            attachment_filename=f'attendance_{filter_date}.csv'
        )
    
    return redirect(url_for('admin_attendance', date=filter_date))

@app.route('/logout')
def logout():
    """Logout user"""
    global current_admin
    current_admin = None
    return redirect(url_for('index'))

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
