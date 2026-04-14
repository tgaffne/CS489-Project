import webview
import threading
import time
import random

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

var polyline = L.polyline(pathCoords, {
    color: 'blue',
    weight: 4
}).addTo(map);

function updateGPS(lat, lon) {
    var point = [lat, lon];

    // add marker
    var marker = L.marker(point).addTo(map);
    markers.push(marker);

    // add to path
    pathCoords.push(point);

    // update polyline
    polyline.setLatLngs(pathCoords);

    // optionally follow latest point
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

def on_closed():
    global running
    running = False

def data_loop(window):
    global running

    lat, lon = 40.4237, -86.9212
    base_hr = 80

    while running:
        time.sleep(2)

        lat += 0.0005
        lon += 0.0005

        hr = base_hr + random.randint(-20, 60)

        window.evaluate_js(f"updateGPS({lat}, {lon});")
        window.evaluate_js(f"updateHR({hr});")

def start():
    window = webview.create_window("Fitness Dashboard", html=HTML)
    window.events.closed += on_closed

    threading.Thread(target=data_loop, args=(window,), daemon=True).start()

    webview.start()

if __name__ == "__main__":
    start()