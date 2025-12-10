/**
 * @file webpage.h
 * @brief Embedded web interface for rover control
 * 
 * This file contains the complete HTML/CSS/JS for the web-based
 * joystick control interface. The webpage is embedded in the binary
 * and served by the HTTP server.
 */

#ifndef WEBPAGE_H
#define WEBPAGE_H

// Complete webpage as a C string
const char webpage_html[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>ESP32 Rover Control</title>
    <style>
        :root {
            --bg-color: #1a1a2e;
            --card-bg: rgba(15, 52, 96, 0.3);
            --accent: #00d9ff;
            --accent-dim: rgba(0, 217, 255, 0.1);
            --text-main: #e0e0e0;
            --border: #0f3460;
            --success: #44ff44;
            --error: #ff4444;
        }

        * { box-sizing: border-box; margin: 0; padding: 0; }
        
        body {
            background-color: var(--bg-color);
            color: var(--text-main);
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            height: 100vh;
            display: flex;
            flex-direction: column;
            overflow-x: hidden;
            touch-action: none;
        }

        /* Header */
        header {
            background: var(--card-bg);
            border-bottom: 2px solid var(--border);
            padding: 1rem;
            display: flex;
            justify-content: space-between;
            align-items: center;
            backdrop-filter: blur(10px);
            position: sticky;
            top: 0;
            z-index: 10;
        }

        h1 { font-size: 1.25rem; color: var(--accent); text-shadow: 0 0 10px rgba(0, 217, 255, 0.5); font-weight: bold; }
        .status-badge { display: flex; align-items: center; gap: 0.5rem; font-size: 0.875rem; font-weight: 500; }
        .status-dot { width: 12px; height: 12px; border-radius: 50%; box-shadow: 0 0 10px currentColor; transition: background-color 0.3s; }
        .connected { background-color: var(--success); color: var(--success); }
        .disconnected { background-color: var(--error); color: var(--error); animation: pulse 2s infinite; }

        /* Main Layout */
        main {
            flex: 1;
            padding: 1rem;
            display: flex;
            flex-direction: column;
            gap: 1rem;
            max-width: 1000px;
            margin: 0 auto;
            width: 100%;
            overflow-y: auto;
        }

        .grid-row { display: grid; grid-template-columns: 1fr; gap: 1rem; }
        @media(min-width: 768px) { .grid-row { grid-template-columns: 1fr 1fr; } }

        .card {
            background: rgba(15, 52, 96, 0.2);
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 1rem;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
        }

        /* Joystick */
        #joystick-container {
            position: relative;
            width: 240px;
            height: 240px;
            border-radius: 50%;
            background: var(--accent-dim);
            border: 3px solid var(--border);
            box-shadow: inset 0 0 30px rgba(0, 217, 255, 0.1);
            touch-action: none;
            cursor: crosshair;
            margin: 10px 0;
        }

        #joystick-knob {
            position: absolute;
            width: 70px;
            height: 70px;
            border-radius: 50%;
            background: linear-gradient(135deg, var(--accent) 0%, var(--border) 100%);
            box-shadow: 0 0 15px rgba(0, 217, 255, 0.5);
            top: 50%; left: 50%;
            transform: translate(-50%, -50%);
            cursor: grab;
        }
        #joystick-knob:active { cursor: grabbing; }

        /* Telemetry Bars */
        .motor-bars { display: flex; width: 100%; gap: 1rem; margin-bottom: 1rem; }
        .motor-group { flex: 1; display: flex; flex-direction: column; align-items: center; }
        .motor-track { 
            width: 100%; height: 100px; 
            background: rgba(0,0,0,0.3); 
            border-radius: 4px; 
            position: relative; 
            overflow: hidden; 
            border: 1px solid var(--border);
            display: flex; align-items: flex-end; justify-content: center;
        }
        .motor-fill { 
            width: 100%; 
            background: linear-gradient(to top, var(--border), var(--accent)); 
            transition: height 0.1s; 
            opacity: 0.9; 
        }
        .motor-label { margin-bottom: 5px; font-size: 0.8rem; color: var(--accent); }
        .motor-value { margin-top: 5px; font-size: 0.9rem; font-family: monospace; }

        /* Telemetry Grid */
        .telemetry-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 0.5rem;
            width: 100%;
        }
        .stat-box {
            background: rgba(0,0,0,0.2);
            border: 1px solid var(--border);
            padding: 0.5rem;
            border-radius: 6px;
            text-align: center;
        }
        .stat-label { font-size: 0.7rem; color: #888; display: block; }
        .stat-value { font-size: 1rem; font-weight: bold; color: #fff; font-family: monospace; }

        /* Charts */
        .chart-container {
            width: 100%;
            height: 180px;
            position: relative;
            background: rgba(0,0,0,0.2);
            border-radius: 8px;
            border: 1px solid var(--border);
            overflow: hidden;
        }
        canvas { display: block; width: 100%; height: 100%; }
        .chart-header { display: flex; justify-content: space-between; width: 100%; align-items: center; margin-bottom: 5px; }
        
        footer { padding: 1rem; text-align: center; font-size: 0.75rem; color: #555; border-top: 1px solid var(--border); margin-top: auto; }

        @keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.5; } 100% { opacity: 1; } }
    </style>
<script type="importmap">
{
  "imports": {
    "react": "https://aistudiocdn.com/react@^19.2.1",
    "react/": "https://aistudiocdn.com/react@^19.2.1/",
    "recharts": "https://aistudiocdn.com/recharts@^3.5.1"
  }
}
</script>
</head>
<body>
    <header>
        <h1>Rover Control</h1>
        <div class="status-badge">
            <div id="status-dot" class="status-dot disconnected"></div>
            <span id="status-text">Disconnected</span>
        </div>
    </header>

    <main>
        <!-- Top Row: Control & Telemetry -->
        <div class="grid-row">
            <!-- Joystick -->
            <div class="card">
                <h2 style="color:var(--accent); font-size: 0.9rem; font-weight: bold; letter-spacing: 1px;">MANUAL CONTROL</h2>
                <div id="joystick-container">
                    <div style="position: absolute; top: 50%; left: 50%; width: 8px; height: 8px; background: var(--accent); border-radius: 50%; transform: translate(-50%, -50%); box-shadow: 0 0 10px var(--accent);"></div>
                    <div id="joystick-knob"></div>
                </div>
                <div id="joystick-debug" style="font-family: monospace; color: var(--accent); font-size: 0.8rem;">Stop</div>
            </div>

            <!-- Telemetry -->
            <div class="card" style="justify-content: flex-start;">
                <h2 style="color:var(--accent); font-size: 0.9rem; font-weight: bold; letter-spacing: 1px; margin-bottom: 15px;">TELEMETRY</h2>
                
                <div class="motor-bars">
                    <div class="motor-group">
                        <span class="motor-label">LEFT</span>
                        <div class="motor-track">
                            <div id="bar-l" class="motor-fill" style="height: 0%"></div>
                        </div>
                        <span id="val-pwm-l" class="motor-value">0</span>
                    </div>
                    <div class="motor-group">
                        <span class="motor-label">RIGHT</span>
                        <div class="motor-track">
                            <div id="bar-r" class="motor-fill" style="height: 0%"></div>
                        </div>
                        <span id="val-pwm-r" class="motor-value">0</span>
                    </div>
                </div>

                <div class="telemetry-grid">
                    <div class="stat-box"><span class="stat-label">L RPM</span><span id="val-lrpm" class="stat-value">0.0</span></div>
                    <div class="stat-box"><span class="stat-label">R RPM</span><span id="val-rrpm" class="stat-value">0.0</span></div>
                    <div class="stat-box"><span class="stat-label">DIST L</span><span id="val-ldist" class="stat-value">0.0m</span></div>
                    <div class="stat-box"><span class="stat-label">DIST R</span><span id="val-rdist" class="stat-value">0.0m</span></div>
                    <div class="stat-box" style="grid-column: span 2;"><span class="stat-label">HEAP</span><span id="val-heap" class="stat-value">-</span></div>
                </div>
            </div>
        </div>

        <!-- Bottom Row: Environmental Sensors -->
        <div class="grid-row">
            <div class="card" style="align-items: stretch;">
                <div class="chart-header">
                    <h3 style="color: #ff6b6b; font-weight: bold;">Temperature</h3>
                    <span id="val-temp" style="color: #ff6b6b; font-weight: bold; font-size: 1.2rem;">--°C</span>
                </div>
                <div class="chart-container">
                    <canvas id="chart-temp"></canvas>
                </div>
            </div>
            <div class="card" style="align-items: stretch;">
                <div class="chart-header">
                    <h3 style="color: #51cf66; font-weight: bold;">Humidity</h3>
                    <span id="val-hum" style="color: #51cf66; font-weight: bold; font-size: 1.2rem;">--%</span>
                </div>
                <div class="chart-container">
                    <canvas id="chart-hum"></canvas>
                </div>
            </div>
        </div>
    </main>

    <footer>
        Embedded ESP32 Controller
    </footer>

    <script>
        // --- Configuration ---
        const CONFIG = {
            wsPort: 8080,
            reconnectMs: 3000,
            chartPoints: 50,
            joystickIntervalMs: 50
        };

        // --- State ---
        let ws = null;
        let reconnectTimer = null;
        const historyTemp = [];
        const historyHum = [];
        
        // --- UI Elements ---
        const ui = {
            statusDot: document.getElementById('status-dot'),
            statusText: document.getElementById('status-text'),
            barL: document.getElementById('bar-l'),
            barR: document.getElementById('bar-r'),
            pwmL: document.getElementById('val-pwm-l'),
            pwmR: document.getElementById('val-pwm-r'),
            lrpm: document.getElementById('val-lrpm'),
            rrpm: document.getElementById('val-rrpm'),
            ldist: document.getElementById('val-ldist'),
            rdist: document.getElementById('val-rdist'),
            heap: document.getElementById('val-heap'),
            temp: document.getElementById('val-temp'),
            hum: document.getElementById('val-hum'),
            joyContainer: document.getElementById('joystick-container'),
            joyKnob: document.getElementById('joystick-knob'),
            joyDebug: document.getElementById('joystick-debug'),
            canvasTemp: document.getElementById('chart-temp'),
            canvasHum: document.getElementById('chart-hum')
        };

        // --- WebSocket ---
        function connectWS() {
            // Determine correct WS URL dynamically
            const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
            // Default to current host, fallback to localhost for dev
            let host = window.location.hostname;
            if (!host) host = 'localhost';
            
            // If running on file:// or standard dev port, assume backend is on port 8080
            // If running on ESP32 (port 80), WS is usually on same port/host at /ws
            const port = (window.location.port === '3000' || window.location.protocol === 'file:') 
                ? `:${CONFIG.wsPort}` 
                : (window.location.port ? `:${window.location.port}` : '');
                
            const url = `${protocol}//${host}${port}/ws`;
            
            console.log('Connecting WS:', url);

            ws = new WebSocket(url);

            ws.onopen = () => {
                console.log('WS Connected');
                ui.statusDot.className = 'status-dot connected';
                ui.statusText.textContent = 'Connected';
                if (reconnectTimer) clearTimeout(reconnectTimer);
            };

            ws.onclose = () => {
                console.log('WS Closed');
                ui.statusDot.className = 'status-dot disconnected';
                ui.statusText.textContent = 'Disconnected';
                reconnectTimer = setTimeout(connectWS, CONFIG.reconnectMs);
            };

            ws.onerror = (e) => {
                // WebSocket errors are often silent in JS for security, but we log what we can
                console.warn('WS Error occurred');
                ws.close();
            };

            ws.onmessage = (e) => {
                try {
                    const msg = JSON.parse(e.data);
                    if (msg.type === 'telemetry') handleTelemetry(msg);
                } catch (err) {
                    console.error('Parse error:', err);
                }
            };
        }

        function sendControl(angle, magnitude) {
            if (ws && ws.readyState === WebSocket.OPEN) {
                const payload = JSON.stringify({
                    type: 'control',
                    angle: parseFloat(angle.toFixed(4)),
                    magnitude: parseFloat(magnitude.toFixed(4)),
                    timestamp: Date.now()
                });
                ws.send(payload);
            }
        }

        // --- Telemetry Handler ---
        function handleTelemetry(data) {
            // Motors
            const lPwm = Math.abs(data.left_pwm || 0);
            const rPwm = Math.abs(data.right_pwm || 0);
            ui.barL.style.height = `${Math.min(lPwm / 255 * 100, 100)}%`;
            ui.barR.style.height = `${Math.min(rPwm / 255 * 100, 100)}%`;
            ui.pwmL.textContent = data.left_pwm || 0;
            ui.pwmR.textContent = data.right_pwm || 0;

            // Stats
            ui.lrpm.textContent = (data.left_rpm || 0).toFixed(1);
            ui.rrpm.textContent = (data.right_rpm || 0).toFixed(1);
            ui.ldist.textContent = (data.left_distance || 0).toFixed(2) + 'm';
            ui.rdist.textContent = (data.right_distance || 0).toFixed(2) + 'm';
            ui.heap.textContent = (data.free_heap || 0).toLocaleString();

            // Sensors & Charts
            if (data.dht_valid) {
                ui.temp.textContent = data.temperature.toFixed(1) + '°C';
                ui.hum.textContent = data.humidity.toFixed(1) + '%';
                
                updateChartData(historyTemp, data.temperature);
                updateChartData(historyHum, data.humidity);
                
                requestAnimationFrame(() => {
                    renderChart(ui.canvasTemp, historyTemp, '#ff6b6b');
                    renderChart(ui.canvasHum, historyHum, '#51cf66');
                });
            }
        }

        // --- Charts (Canvas API) ---
        // Resizes canvas to match display size for sharp rendering on HDPI
        function resizeCanvas(canvas) {
            const rect = canvas.getBoundingClientRect();
            const dpr = window.devicePixelRatio || 1;
            
            if (canvas.width !== rect.width * dpr || canvas.height !== rect.height * dpr) {
                canvas.width = rect.width * dpr;
                canvas.height = rect.height * dpr;
            }
        }

        function updateChartData(buffer, value) {
            buffer.push(value);
            if (buffer.length > CONFIG.chartPoints) buffer.shift();
        }

        function renderChart(canvas, data, color) {
            resizeCanvas(canvas);
            const ctx = canvas.getContext('2d');
            const w = canvas.width;
            const h = canvas.height;
            
            ctx.clearRect(0, 0, w, h);
            if (data.length < 2) return;

            // Determine Y scale
            let min = Math.min(...data);
            let max = Math.max(...data);
            let range = max - min;
            if (range === 0) range = 1;
            
            // Add padding
            min -= range * 0.1;
            max += range * 0.1;
            range = max - min;

            // Draw line
            ctx.beginPath();
            ctx.strokeStyle = color;
            ctx.lineWidth = 3;
            ctx.lineJoin = 'round';

            const stepX = w / (data.length - 1);
            
            data.forEach((val, i) => {
                const x = i * stepX;
                const y = h - ((val - min) / range * h);
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            });
            ctx.stroke();

            // Fill area
            ctx.lineTo(w, h);
            ctx.lineTo(0, h);
            ctx.closePath();
            const grad = ctx.createLinearGradient(0, 0, 0, h);
            grad.addColorStop(0, color);
            grad.addColorStop(1, 'transparent');
            ctx.fillStyle = grad;
            ctx.globalAlpha = 0.2;
            ctx.fill();
            ctx.globalAlpha = 1.0;
        }

        // --- Joystick Logic ---
        let isDragging = false;
        const joyState = { angle: 0, magnitude: 0 };

        function initJoystick() {
            const container = ui.joyContainer;
            
            const start = (e) => {
                isDragging = true;
                move(e);
            };

            const move = (e) => {
                if (!isDragging) return;
                // e.preventDefault(); // Uncomment if touch scrolling becomes an issue

                const rect = container.getBoundingClientRect();
                const centerX = rect.width / 2;
                const centerY = rect.height / 2;
                
                const clientX = e.touches ? e.touches[0].clientX : e.clientX;
                const clientY = e.touches ? e.touches[0].clientY : e.clientY;

                let dx = clientX - rect.left - centerX;
                let dy = clientY - rect.top - centerY;
                
                const maxR = (rect.width / 2) - 35; // 35 is half knob size
                const distance = Math.sqrt(dx*dx + dy*dy);

                // Clamp
                if (distance > maxR) {
                    const ratio = maxR / distance;
                    dx *= ratio;
                    dy *= ratio;
                }

                // Visual Update
                ui.joyKnob.style.transform = `translate(calc(-50% + ${dx}px), calc(-50% + ${dy}px))`;

                // Calc Control Values
                // Angle: Right=0, Up=PI/2 (Note: Screen Y is down, so we invert DY)
                let angle = Math.atan2(-dy, dx); 
                if (angle < 0) angle += 2 * Math.PI;
                
                const magnitude = Math.min(distance / maxR, 1.0);

                joyState.angle = angle;
                joyState.magnitude = magnitude;

                const deg = Math.round(angle * 180 / Math.PI);
                const mag = Math.round(magnitude * 100);
                ui.joyDebug.textContent = `Angle: ${deg}° | Speed: ${mag}%`;
            };

            const end = () => {
                if (!isDragging) return;
                isDragging = false;
                
                // Reset Visuals
                ui.joyKnob.style.transform = `translate(-50%, -50%)`;
                ui.joyDebug.textContent = "Stop";
                
                // Reset State
                joyState.angle = 0;
                joyState.magnitude = 0;
                
                // Send Stop immediately
                sendControl(0, 0);
            };

            container.addEventListener('mousedown', start);
            container.addEventListener('touchstart', start, {passive: false});
            
            window.addEventListener('mousemove', move);
            window.addEventListener('touchmove', move, {passive: false});
            
            window.addEventListener('mouseup', end);
            window.addEventListener('touchend', end);
        }

        // --- Init ---
        initJoystick();
        connectWS();

        // Transmission Loop
        setInterval(() => {
            if (isDragging) {
                sendControl(joyState.angle, joyState.magnitude);
            }
        }, CONFIG.joystickIntervalMs);

    </script>
</body>
</html>
)rawliteral";

#endif // WEBPAGE_H