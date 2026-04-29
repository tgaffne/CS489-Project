import webview
import threading
import time
import json
import os
import serial
from datetime import datetime

MAPS_DIR = "maps"
EOF_MARKER = "END"
os.makedirs(MAPS_DIR, exist_ok=True)

ser = None

# UI

# HTML for homescreen
HOME_HTML = """
<!DOCTYPE html>
<html>
<body style="
    margin:0;
    height:100vh;
    display:flex;
    justify-content:center;
    align-items:center;
    background:#f5f5f5;
    font-family:sans-serif;
">

<div style="
    background:white;
    padding:30px;
    border-radius:12px;
    box-shadow:0 4px 20px rgba(0,0,0,0.1);
    width:300px;
    text-align:center;
    display:flex;
    flex-direction:column;
    gap:15px;
">
    <h1>Fitness Dashboard</h1>

    <button onclick="openLive()">Upload Workout from Device</button>
    <button onclick="showMaps()">Open Saved Workouts</button>

    <div id="mapList"></div>
</div>

<script>
async function openLive() {
    await window.pywebview.api.open_map();
}

// Function to display the list of old workouts
async function showMaps() {
    const files = await window.pywebview.api.list_maps();

    const container = document.getElementById("mapList");
    container.innerHTML = "";

    files.forEach(file => {
        const btn = document.createElement("button");
        btn.innerText = file;
        btn.onclick = () => openOld(file);
        container.appendChild(btn);
    });
}

// Function to open an old workout
async function openOld(filename) {
    await window.pywebview.api.open_old_map(filename);
}

</script>

<input type="file" id="fileInput" style="display:none;" />

</body>
</html>
"""

# HTML for displaying map data
HTML = """
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8" />
<title>Fitness Dashboard</title>

// Leaflet library used for GPS data
<script src="https://unpkg.com/leaflet/dist/leaflet.js"></script>
<link rel="stylesheet" href="https://unpkg.com/leaflet/dist/leaflet.css"/>

// Chart library used for HR data
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<script src="https://cdn.jsdelivr.net/npm/chartjs-plugin-datalabels@2"></script>

<style>
body {
    margin: 0;
    height: 100vh;
    display: flex;
    flex-direction: column;
    font-family: sans-serif;
}

#map {
    height: 70%;
    width: 100%;
}

#avgHR {
    padding: 5px;
    color: #333;
    background: white;
}

#chartContainer {
    height: 30%;
    width: 100%;
    background: white;
    padding: 10px;
    box-sizing: border-box;
}

canvas {
    width: 100% !important;
    height: 100% !important;
    
}
</style>
</head>

<body>

<div id="map"></div>

<div id="avgHR" style="font-size: 18px; font-weight: bold;">
    Avg: -- BPM
</div>

<div id="chartContainer">
    <canvas id="hrChart"></canvas>
</div>

<script>

// Function to print data onto UI (GPS on top / HR on bottom)
function loadMapData(content) {
    const data = JSON.parse(content);

    const totalPoints = Math.max(
        data.gps ? data.gps.length : 0,
        data.hr ? data.hr.length : 0
    );

    if (totalPoints === 0) return;

    // Scale print time relative to number of data points
    const totalReplayTime = 10000;
    let delay = totalReplayTime / totalPoints;
    delay = Math.max(30, Math.min(delay, 500));

    let i = 0;

    // Print GPS / HR data from data point by point
    function step() {
        if (data.gps && i < data.gps.length) {
            const p = data.gps[i];
            updateGPS(p.lat, p.lon);
        }

        if (data.hr && i < data.hr.length) {
            updateHR(data.hr[i]);
        }

        i++;

        if (
            (data.gps && i < data.gps.length) ||
            (data.hr && i < data.hr.length)
        ) {
            setTimeout(step, delay);
        }
    }

    step();
}

// Variables and functions for map display
var map = L.map('map').setView([40.4237, -86.9212], 15);

L.tileLayer('https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}{r}.png', {
}).addTo(map);

var markers = [];
var pathCoords = [];
var lastMarkerLatLng = null;
var MARKER_DISTANCE_THRESHOLD = 100; 

// Line between points
var polyline = L.polyline(pathCoords, {
    color: 'blue',
    weight: 4,
    smoothFactor: 1.0,
    lineCap: 'round',
    lineJoin: 'round',
    renderer: L.svg()
}).addTo(map);

function updateGPS(lat, lon) {
    var point = [lat, lon];
    var latlng = L.latLng(lat, lon);

    // Draw line to new point
    pathCoords.push(point);
    polyline.setLatLngs(pathCoords);

    let shouldAddMarker = false;

    // Add marker based on distance to previous marker
    if (!lastMarkerLatLng) {
        shouldAddMarker = true;
    } else {
        const distance = latlng.distanceTo(lastMarkerLatLng); // meters
        if (distance >= MARKER_DISTANCE_THRESHOLD) {
            shouldAddMarker = true;
        }
    }

    // Add the marker if heuristic is true
    if (shouldAddMarker) {
        const timestamp = new Date().toLocaleTimeString();

        var marker = L.marker(point).addTo(map).bindPopup(timestamp);

        markers.push(marker);
        lastMarkerLatLng = latlng;
    }

    // Center view on new point
    map.setView(point);
}

// Variables and functions for HR display
const ctx = document.getElementById('hrChart').getContext('2d');

const data = {
    labels: [],
    datasets: [{
        label: 'Heart Rate (BPM)',
        data: [],
        borderColor: 'red',
        tension: 0,
        fill: false
    }]
};

// Create HR display
const chart = new Chart(ctx, {
    type: 'line',
    data: data,
    options: {
        animation: false,
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
            datalabels: {
                align: 'top',
                anchor: 'end',
                color: 'black',
                font: {
                    weight: 'bold',
                    size: 10
                },
                formatter: function(value) {
                    return value; // show BPM value
                }
            }
        },
        scales: {
            y: {
                suggestedMin: 50,
                suggestedMax: 180
            }
        }
    },
    plugins: [ChartDataLabels]
});

// Function to update the HR display
function updateHR(value) {
    const now = new Date().toLocaleTimeString();

    data.labels.push(now);
    data.datasets[0].data.push(value);

    // Shift heart rate display when too many points shown
    if (data.labels.length > 15) {
        data.labels.shift();
        data.datasets[0].data.shift();
    }

    const arr = data.datasets[0].data;
    const sum = arr.reduce((a, b) => a + b, 0);
    const avg = Math.round(sum / arr.length);

    document.getElementById("avgHR").innerText = "Avg: " + avg + " BPM";
    chart.update();
}
</script>

</body>
</html>
"""

# BACKEND

session_data = {
    "gps": [],
    "hr": []
}

# API used by the HTML UI
class Api:
    # Helper function to open a map when uploading data
    def open_map(self):
        global map_window, session_data

        session_data = {"gps": [], "hr": []}

        map_window = webview.create_window(
            "Live Map",
            html=HTML,
            width=1000,
            height=700
        )

        threading.Thread(
            target=self.run_upload_session,
            kwargs={"port": "COM3", "baud": 115200},
            daemon=True
        ).start()

    # Helper function to list all past workouts saved in MAP directory
    def list_maps(self):
        files = os.listdir(MAPS_DIR)
        return files

    # Helper function to open an old workout saved in the Map directory (from filename)
    def open_old_map(self, filename):
        global map_window

        path = os.path.join(MAPS_DIR, filename)
        print(path)

        with open(path, "r") as f:
            content = f.read()

        map_window = webview.create_window(
            f"Map - {filename}",
            html=HTML
        )

        def load_data():
            time.sleep(1)
            map_window.evaluate_js(f"loadMapData({json.dumps(content)});")

        threading.Thread(target=load_data).start()

    # Helper function to save maps from JSON data
    def save_map(self, content):
        timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        filename = f"{timestamp}.json"

        path = os.path.join(MAPS_DIR, filename)

        with open(path, "w") as f:
            f.write(content)

        return filename
    
    # Function to interpret GPS / HR data uploaded over serial connection
    def run_upload_session(self, port="COM3", baud=115200):
        global ser, session_data, map_window

        ser = None

        try:
            # Attempt serial connection
            ser = serial.Serial(port, baud, timeout=1)
            print(f"[Serial] Connected to {port} @ {baud}")

            # Accept serial data line by line and parse
            while True:
                line = ser.readline().decode(errors="ignore").strip()

                if not line:
                    continue

                if line == EOF_MARKER:
                    print("[Serial] EOF received — ending session")
                    break

                parts = line.split(",")

                if len(parts) < 4:
                    print("Bad packet:", line)
                    continue

                # Attempt to parse HR data
                try:
                    hr = int(parts[3])
                except Exception as e:
                    print("Parse error:", e, "Line:", line)
                    continue

                session_data["hr"].append(hr)

                # Update HR data
                if map_window:
                    try:
                        map_window.evaluate_js(f"updateHR({hr});")
                    except Exception as e:
                        print("Update failed:", e)
                
                # Attempt to parse GPS data
                try:
                    lat = float(parts[0])
                    lon = float(parts[1])
                except Exception as e:
                    print("Parse error:", e, "Line:", line)
                    continue

                session_data["gps"].append({"lat": lat, "lon": lon})

                # Update GPS data
                if map_window:
                    try:
                        if (lat != 0 and lon != 0):
                            map_window.evaluate_js(f"updateGPS({lat}, {lon});")
                    except Exception as e:
                        print("Update failed:", e)

        except Exception as e:
            print("Connection error:", e)

        # Close connection once EOF has been recieved
        finally:
            if ser:
                try:
                    if ser.is_open:
                        ser.close()
                except Exception as e:
                    print("Close error:", e)

            ser = None
            print("Closed session")

            # Save workout once connection is closed
            if session_data["gps"] or session_data["hr"]:
                try:
                    api = Api()
                    content = json.dumps(session_data)
                    filename = api.save_map(content)
                    print(f"[Save] Workout saved: {filename}")
                except Exception as e:
                    print("[Save] Failed:", e)

# Function to define behaviour when a window closes
def on_closed():
    global ser

    # Close serial connection if user is uploading data
    if ser:
        try:
            ser.close()
        except:
            pass

    # Save GPS / HR data if user has uploaded any
    if session_data["gps"] or session_data["hr"]:
        api = Api()
        content = json.dumps(session_data)
        filename = api.save_map(content)
        print(f"Saved workout to {filename}")

# Function to start application
def start():
    global window

    api = Api()

    # Create home window
    window = webview.create_window(
        "Fitness Dashboard",
        html=HOME_HTML,
        js_api=api,
        width=400,
        height=300
    )

    # Save GPS and HR data when the window is closed
    window.events.closed += on_closed
    webview.start()

if __name__ == "__main__":
    start()