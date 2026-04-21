import webview
import threading
import time
import random
import asyncio
import websockets
import json

running = True

HTML = """
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8" />
<title>Fitness Dashboard</title>

<script src="https://unpkg.com/leaflet/dist/leaflet.js"></script>
<link rel="stylesheet" href="https://unpkg.com/leaflet/dist/leaflet.css"/>

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


/* Top half = Map */
#map {
    height: 70%;
    width: 100%;
}

#avgHR {
    padding: 5px;
    color: #333;
    background: white;
}

/* Bottom half = Chart */
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

// ---------------- MAP ----------------
var map = L.map('map').setView([40.4237, -86.9212], 15);

L.tileLayer('https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}{r}.png', {
}).addTo(map);

var markers = [];
var pathCoords = [];
var lastMarkerLatLng = null;
var MARKER_DISTANCE_THRESHOLD = 300; // meters (tune this)

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

    pathCoords.push(point);
    polyline.setLatLngs(pathCoords);

    let shouldAddMarker = false;

    if (!lastMarkerLatLng) {
        shouldAddMarker = true;
    } else {
        const distance = latlng.distanceTo(lastMarkerLatLng); // meters
        if (distance >= MARKER_DISTANCE_THRESHOLD) {
            shouldAddMarker = true;
        }
    }

    if (shouldAddMarker) {
        const timestamp = new Date().toLocaleTimeString();

        var marker = L.marker(point).addTo(map).bindPopup(timestamp);

        markers.push(marker);
        lastMarkerLatLng = latlng;
    }

    map.setView(point);
}

// ---------------- HEART RATE ----------------
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
    plugins: [ChartDataLabels]   // IMPORTANT
});

function updateHR(value) {
    const now = new Date().toLocaleTimeString();

    data.labels.push(now);
    data.datasets[0].data.push(value);

    if (data.labels.length > 15) {
        data.labels.shift();
        data.datasets[0].data.shift();
    }

    // ---- calculate average ----
    const arr = data.datasets[0].data;
    const sum = arr.reduce((a, b) => a + b, 0);
    const avg = Math.round(sum / arr.length);

    // update UI
    document.getElementById("avgHR").innerText = "Avg: " + avg + " BPM";

    chart.update();
}
</script>

</body>
</html>
"""

# ---------------- PYTHON BACKEND ----------------

async def ws_handler(websocket):
    async for message in websocket:
        try:
            data = json.loads(message)

            lat = data.get("lat")
            lon = data.get("lon")
            hr = data.get("hr")

            if lat is not None and lon is not None:
                window.evaluate_js(f"updateGPS({lat}, {lon});")

            if hr is not None:
                window.evaluate_js(f"updateHR({hr});")

        except Exception as e:
            print("Error:", e)

async def ws_main():
    async with websockets.serve(ws_handler, "0.0.0.0", 8765):
        print("WebSocket server running on ws://0.0.0.0:8765")
        await asyncio.Future()  # run forever

def start_ws_server():
    asyncio.run(ws_main())


def on_closed():
    global running
    running = False

def start():
    global window

    window = webview.create_window("Fitness Dashboard", html=HTML)
    window.events.closed += on_closed

    threading.Thread(target=start_ws_server, daemon=True).start()

    webview.start()

if __name__ == "__main__":
    start()