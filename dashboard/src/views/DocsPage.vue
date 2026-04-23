<template>
  <div class="docs-page">
    <div class="docs-layout">
      <!-- Sidebar TOC -->
      <aside class="docs-sidebar" :class="{ 'sidebar-open': sidebarOpen }">
        <button class="sidebar-toggle" @click="sidebarOpen = !sidebarOpen">
          <Menu :size="18" />
          <span>Contents</span>
          <ChevronDown :size="14" class="toggle-chevron" :class="{ rotated: sidebarOpen }" />
        </button>
        <div class="sidebar-brand">
          <router-link to="/" class="sidebar-brand-link">
            <img src="/logo.png" alt="Senseware" class="sidebar-logo" />
            <span class="sidebar-brand-name">Senseware</span>
            <ArrowLeft :size="14" class="sidebar-home-icon" />
          </router-link>
          <p class="sidebar-home-hint">Back to Home</p>
        </div>
        <nav class="toc-nav" v-show="sidebarOpen || !isMobile">
          <ul class="toc-list">
            <li v-for="section in sections" :key="section.id">
              <a
                :href="'#' + section.id"
                :class="{ active: activeSection === section.id }"
                @click.prevent="scrollTo(section.id)"
              >{{ section.title }}</a>
            </li>
          </ul>
        </nav>
      </aside>

      <!-- Main Content -->
      <main class="docs-main">
        <!-- Section 1: System Overview -->
        <section id="overview" class="doc-section doc-section-alt" data-section="overview">
          <div class="section-label">Overview</div>
          <h2 class="section-heading">System Overview</h2>
          <p class="section-text">
            Senseware is a closed-loop wearable system designed for real-time stress detection in
            individuals on the autism spectrum. It combines biosensor data (EMG, heart rate, motion)
            with on-device machine learning to detect physiological anomalies before they escalate.
            Caregivers receive alerts through a lightweight web dashboard, enabling timely and gentle intervention.
          </p>
          <MermaidDiagram :chart="overviewChart" />
        </section>

        <!-- Section 2: Hardware -->
        <section id="hardware" class="doc-section" data-section="hardware">
          <div class="section-label">Hardware</div>
          <h2 class="section-heading">Hardware Components</h2>
          <p class="section-text">
            The wearable is built around the YD-ESP32 Type-A (ESP32-WROOM-32), with all sensors communicating
            over a shared I2C bus (except EMG which uses analog input). Below is the complete hardware manifest.
          </p>
          <div class="table-wrap">
            <table class="doc-table">
              <thead>
                <tr>
                  <th>Component</th>
                  <th>Model</th>
                  <th>Interface</th>
                  <th>Pin</th>
                  <th>Purpose</th>
                </tr>
              </thead>
              <tbody>
                <tr><td>MCU</td><td>YD-ESP32 Type-A</td><td>&mdash;</td><td>&mdash;</td><td>Main processor, WiFi, ML inference</td></tr>
                <tr><td>EMG Sensor</td><td>OYMotion SEN0240</td><td>Analog GPIO 34</td><td>EMG_PIN</td><td>Muscle tension measurement</td></tr>
                <tr><td>PPG Heart Rate</td><td>MAX30102</td><td>I2C (0x57)</td><td>SDA=21, SCL=22</td><td>Heart rate (beat detection) + SpO2 (red/IR ratio)</td></tr>
                <tr><td>IMU</td><td>MPU-9250</td><td>I2C (0x68)</td><td>SDA=21, SCL=22</td><td>Motion magnitude</td></tr>
                <tr><td>Display</td><td>SH1106 OLED 128×64</td><td>I2C (0x3C)</td><td>SDA=21, SCL=22</td><td>Real-time metrics</td></tr>
                <tr><td>Haptic Motor</td><td>LRA</td><td>LEDC PWM</td><td>GPIO 25</td><td>Gentle vibration alerts</td></tr>
                <tr><td>Power</td><td>TP4056 + 3.7V 1600mAh LiPo</td><td>&mdash;</td><td>VIN</td><td>Battery power with charging</td></tr>
              </tbody>
            </table>
          </div>
          <MermaidDiagram :chart="i2cChart" />
          <div class="image-grid three-col">
            <img src="/images/hardware/enclosure_case_top_view.jpg" alt="3D-printed enclosure case, top view" class="doc-image" />
            <img src="/images/hardware/enclosure_curved_panel.jpg" alt="3D-printed curved panel" class="doc-image" />
            <img src="/images/hardware/enclosure_sensor_window.jpg" alt="3D-printed sensor window panel" class="doc-image" />
          </div>
          <h3 class="sub-heading">Electronics</h3>
          <div class="doc-image-single">
            <img src="/images/hardware/esp32_breadboard_complete.jpg" alt="Complete ESP32 breadboard circuit" />
          </div>
        </section>

        <!-- Section 3: Firmware Architecture -->
        <section id="firmware" class="doc-section" data-section="firmware">
          <div class="section-label">Firmware</div>
          <h2 class="section-heading">Firmware Architecture</h2>
          <p class="section-text">
            The firmware runs on FreeRTOS with a dual-core architecture. Core 0 handles the high-frequency
            EMG sampling task (1000 Hz), while Core 1 manages sensor polling, display updates, serial output,
            ML inference, and the HTTP server. All tasks communicate through a shared <code>PhysioState</code> struct
            protected by a mutex.
          </p>
          <MermaidDiagram :chart="firmwareChart" />
          <h3 class="sub-heading">PhysioState Struct</h3>
          <div class="code-block"><pre><span class="code-kw">struct</span> <span class="code-type">PhysioState</span> {
  <span class="code-type">float</span> heart_rate;
  <span class="code-type">float</span> spo2;
  <span class="code-type">float</span> emg_envelope;
  <span class="code-type">float</span> motion_magnitude;
  <span class="code-type">unsigned long</span> timestamp;
};
<span class="code-cmt">// MSE and anomaly flag are global variables, not struct fields</span></pre></div>
          <p class="section-text">
            The shared state is protected by a FreeRTOS mutex. When the EMG task acquires the mutex, it writes
            the latest filtered EMG value; the sensor task writes heart rate, SpO2, and motion magnitude.
            The inference task reads the full struct to feed the autoencoder. A software timeout in the display task monitors the
            I2C bus; if a display cycle takes longer than 2 seconds (indicating a hung bus), it recovers the I2C peripheral and
            reinitializes the OLED display.
          </p>
        </section>

        <!-- Section 4: Signal Processing -->
        <section id="signal-processing" class="doc-section doc-section-alt" data-section="signal-processing">
          <div class="section-label">Signal Processing</div>
          <h2 class="section-heading">Signal Processing Pipeline</h2>
          <h3 class="sub-heading">EMG Processing</h3>
          <p class="section-text">
            Raw EMG is sampled at 1000 Hz from the OYMotion SEN0240 sensor via the ESP32's 12-bit ADC.
            The OYMotion EMGFilters library applies a bandpass filter (HPF 20 Hz, LPF 150 Hz) with a 50 Hz notch
            for mains rejection. A 3-second rest baseline calibrates the noise floor. After 2000 samples of filter
            warm-up, the rectified squared envelope is smoothed with an exponential moving average (α = 0.01).
          </p>
          <MermaidDiagram :chart="emgChart" />

          <h3 class="sub-heading">Motion Processing</h3>
          <p class="section-text">
            The MPU-9250 accelerometer data is polled at ~12.5 Hz (every other cycle of the 25 Hz sensor task). The three-axis acceleration values
            (ax, ay, az) are combined into a single motion magnitude vector:
          </p>
          <div class="code-block"><pre>motion_magnitude = <span class="code-fn">sqrt</span>(ax² + ay² + az²)</pre></div>

          <h3 class="sub-heading">Heart Rate & SpO2</h3>
          <p class="section-text">
            The bare MAX30102 PPG chip is polled at 25Hz via the SparkFun MAX30105 library.
            Heart rate is detected using the SparkFun heartRate library's checkForBeat() algorithm
            on the IR photodiode signal. SpO2 is computed from the ratio of Red and IR DC components
            using the Maxim AN6407 empirical formula.
          </p>
        </section>

        <!-- Section 5: Machine Learning -->
        <section id="ml" class="doc-section doc-section-alt" data-section="ml">
          <div class="section-label">Machine Learning</div>
          <h2 class="section-heading">Machine Learning Pipeline</h2>
          <p class="section-text">
            The anomaly detection model is a lightweight autoencoder trained on the user's personal
            calm baseline data. It learns to reconstruct normal physiological patterns. Instances of high reconstruction
            error indicate deviation from the learned baseline.
          </p>
          <MermaidDiagram :chart="mlPipelineChart" />

          <h3 class="sub-heading">Autoencoder Architecture</h3>
          <MermaidDiagram :chart="autoencoderChart" />
          <div class="spec-grid">
            <div class="spec-item"><span class="spec-label">Parameters</span><span class="spec-value">395 total</span></div>
            <div class="spec-item"><span class="spec-label">Model Size</span><span class="spec-value">4.1 KB TFLite</span></div>
            <div class="spec-item"><span class="spec-label">Tensor Arena</span><span class="spec-value">8 KB, 16-byte aligned</span></div>
            <div class="spec-item"><span class="spec-label">Inference Rate</span><span class="spec-value">0.5 Hz (every 2s)</span></div>
          </div>

          <h3 class="sub-heading">Normalization Constants</h3>
          <div class="table-wrap">
            <table class="doc-table">
              <thead><tr><th>Feature</th><th>Mean (μ)</th><th>Std Dev (σ)</th></tr></thead>
              <tbody>
                <tr><td>Heart Rate</td><td>71.6</td><td>1.0</td></tr>
                <tr><td>EMG Envelope</td><td>6.6</td><td>146.2</td></tr>
                <tr><td>Motion Magnitude</td><td>1.02</td><td>0.04</td></tr>
              </tbody>
            </table>
          </div>

          <h3 class="sub-heading">Adaptive Threshold</h3>
          <p class="section-text">
            The initial anomaly detection threshold is MSE &gt; <strong>0.092563</strong> (mean + 3σ of baseline error).
            The system maintains a rolling window of 200 MSE samples and recalculates the threshold every 30 seconds.
            The updated threshold is persisted to NVS (Non-Volatile Storage) so it survives power cycles.
          </p>
        </section>

        <!-- Section 6: Anomaly Detection -->
        <section id="anomaly-detection" class="doc-section" data-section="anomaly-detection">
          <div class="section-label">Detection Logic</div>
          <h2 class="section-heading">Anomaly Detection Logic</h2>
          <p class="section-text">
            The anomaly detection uses a windowed confirmation approach to prevent false triggers
            from brief sensor spikes. Three consecutive MSE values above threshold open a 5-second
            confirmation window. If anomalies persist for the full window duration (or 3+ events occur
            within the window), an alert is triggered, followed by a 15-second cooldown.
          </p>
          <MermaidDiagram :chart="anomalyChart" />
          <div class="spec-grid">
            <div class="spec-item"><span class="spec-label">Confirmation Count</span><span class="spec-value">3 consecutive samples</span></div>
            <div class="spec-item"><span class="spec-label">Window Duration</span><span class="spec-value">5 seconds</span></div>
            <div class="spec-item"><span class="spec-label">Cooldown</span><span class="spec-value">15 seconds between alerts</span></div>
            <div class="spec-item"><span class="spec-label">Haptic Pattern</span><span class="spec-value">Double-pulse (220ms on, 180ms gap, 220ms on)</span></div>
          </div>
        </section>

        <!-- Section 7: Communication Protocol -->
        <section id="communication" class="doc-section doc-section-alt" data-section="communication">
          <div class="section-label">Communication</div>
          <h2 class="section-heading">Communication Protocol</h2>
          <p class="section-text">
            The ESP32 communicates with the caregiver dashboard via HTTP Server-Sent Events (SSE).
            The ESP32 hosts a lightweight HTTP server on port 81 with three endpoints. The persistent SSE
            connection provides real-time telemetry push without the overhead of WebSocket handshakes.
          </p>
          <MermaidDiagram :chart="sseChart" />

          <h3 class="sub-heading">Endpoints</h3>
          <div class="table-wrap">
            <table class="doc-table">
              <thead><tr><th>Endpoint</th><th>Method</th><th>Description</th></tr></thead>
              <tbody>
                <tr><td><code>/</code></td><td>GET</td><td>Status page with links to /events and /telemetry</td></tr>
                <tr><td><code>/events</code></td><td>GET</td><td>Persistent SSE stream (JSON every 1s, keepalive every 3s)</td></tr>
                <tr><td><code>/telemetry</code></td><td>GET</td><td>Single-shot JSON snapshot of current state</td></tr>
              </tbody>
            </table>
          </div>

          <h3 class="sub-heading">SSE Payload Format</h3>
          <div class="doc-callout">
            <p class="section-text" style="margin-bottom: 0.75rem;">
              Each SSE event delivers a JSON telemetry snapshot with the following fields:
            </p>
            <div class="code-block" style="margin: 0;"><pre>{
  <span class="code-key">"hr"</span>: <span class="code-num">72.0</span>,
  <span class="code-key">"spo2"</span>: <span class="code-num">98.0</span>,
  <span class="code-key">"emg"</span>: <span class="code-num">50.0</span>,
  <span class="code-key">"mot"</span>: <span class="code-num">1.03</span>,
  <span class="code-key">"mse"</span>: <span class="code-num">0.0005</span>,
  <span class="code-key">"acc"</span>: <span class="code-num">95.0</span>,
  <span class="code-key">"anomaly"</span>: <span class="code-num">0</span>
}</pre></div>
          </div>
          <div class="doc-callout">
            <p class="section-text" style="margin: 0;">
              The <code>acc</code> field is a heuristic confidence metric (0-100%) computed as
              100 &times; e<sup>-MSE/threshold</sup>, indicating how closely the current reading
              matches the learned baseline.
            </p>
          </div>
          <p class="section-text">
            CORS is enabled with <code>Access-Control-Allow-Origin: *</code> to allow cross-origin access from any
            browser connecting to the ESP32's WiFi access point.
          </p>
        </section>

        <!-- Section 8: Dashboard -->
        <section id="dashboard" class="doc-section" data-section="dashboard">
          <div class="section-label">Dashboard</div>
          <h2 class="section-heading">Caregiver Dashboard</h2>
          <p class="section-text">
            The caregiver-facing web application is built with Vue 3 and Vite. It connects to the ESP32
            via the EventSource API (SSE) and renders real-time telemetry charts using Chart.js. The dashboard
            is designed to be mobile-friendly for use on phones and tablets.
          </p>
          <div class="feature-grid">
            <div class="feature-card">
              <h4 class="feature-title">Live Telemetry</h4>
              <p class="feature-desc">Chart.js rolling line chart (30s to 5m window) with 3 y-axes for HR/SpO2, EMG, and motion magnitude.</p>
            </div>
            <div class="feature-card">
              <h4 class="feature-title">Vitals Cards</h4>
              <p class="feature-desc">Real-time display of heart rate, SpO2, EMG tension, and motion with color-coded status.</p>
            </div>
            <div class="feature-card">
              <h4 class="feature-title">Alert Log</h4>
              <p class="feature-desc">Timestamped list of anomaly alerts with rising-edge detection on the anomaly field (0 → 1).</p>
            </div>
            <div class="feature-card">
              <h4 class="feature-title">Connection Panel</h4>
              <p class="feature-desc">SSE connection manager with 2-second reconnect debounce for silent recovery.</p>
            </div>
          </div>
          <div class="doc-image-single">
            <img src="/images/dashboard/Screenshot 2026-04-24 050614.png" alt="Senseware caregiver dashboard" />
          </div>
          <h3 class="sub-heading">Real-Time Components</h3>
          <div class="table-wrap">
            <table class="doc-table">
              <thead><tr><th>Component</th><th>Purpose</th></tr></thead>
              <tbody>
                <tr><td><code>VitalsCard</code></td><td>Displays individual vital metric with icon, value, and unit</td></tr>
                <tr><td><code>LiveChart</code></td><td>Chart.js rolling line chart with multi-axis telemetry</td></tr>
                <tr><td><code>AlertLog</code></td><td>Scrollable list of timestamped anomaly events</td></tr>
                <tr><td><code>ConnectionPanel</code></td><td>SSE URL input, connect/disconnect controls, status indicator</td></tr>
              </tbody>
            </table>
          </div>
        </section>

        <!-- Section 9: Power System -->
        <section id="power" class="doc-section doc-section-alt" data-section="power">
          <div class="section-label">Power</div>
          <h2 class="section-heading">Power System</h2>
          <p class="section-text">
            The wearable is powered by a 3.7V 1600mAh LiPo battery managed by a TP4056 (ZJ-CHC-V2) charging board.
            The battery connects to the ESP32 board's VIN pin. A bulk capacitor (100 to 470µF) across VIN-GND is
            recommended to absorb WiFi transmit current transients.
          </p>

          <div class="spec-grid">
            <div class="spec-item"><span class="spec-label">Battery</span><span class="spec-value">3.7V 1600mAh LiPo</span></div>
            <div class="spec-item"><span class="spec-label">Charger</span><span class="spec-value">TP4056 (ZJ-CHC-V2)</span></div>
            <div class="spec-item"><span class="spec-label">WiFi Active</span><span class="spec-value">~4.5 hours</span></div>
            <div class="spec-item"><span class="spec-label">Sensors Only</span><span class="spec-value">~16 hours</span></div>
          </div>
        </section>
      </main>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, onUnmounted } from 'vue'
import { Menu, ChevronDown, ArrowLeft } from 'lucide-vue-next'
import MermaidDiagram from '../components/MermaidDiagram.vue'

// ===== TOC Sections =====
const sections = [
  { id: 'overview', title: 'System Overview' },
  { id: 'hardware', title: 'Hardware' },
  { id: 'firmware', title: 'Firmware Architecture' },
  { id: 'signal-processing', title: 'Signal Processing' },
  { id: 'ml', title: 'Machine Learning' },
  { id: 'anomaly-detection', title: 'Anomaly Detection' },
  { id: 'communication', title: 'Communication Protocol' },
  { id: 'dashboard', title: 'Dashboard' },
  { id: 'power', title: 'Power System' },
]

// ===== Sidebar State =====
const sidebarOpen = ref(false)
const isMobile = ref(false)
const activeSection = ref('overview')

function checkMobile() {
  isMobile.value = window.innerWidth < 900
}

// ===== Scroll spy with IntersectionObserver =====
let observer = null

onMounted(() => {
  checkMobile()
  window.addEventListener('resize', checkMobile)

  observer = new IntersectionObserver(
    (entries) => {
      entries.forEach((entry) => {
        if (entry.isIntersecting) {
          activeSection.value = entry.target.dataset.section
        }
      })
    },
    { rootMargin: '-80px 0px -60% 0px', threshold: 0 }
  )

  document.querySelectorAll('.doc-section').forEach((el) => observer.observe(el))
})

onUnmounted(() => {
  window.removeEventListener('resize', checkMobile)
  if (observer) observer.disconnect()
})

function scrollTo(id) {
  const el = document.getElementById(id)
  if (el) {
    el.scrollIntoView({ behavior: 'smooth', block: 'start' })
    if (isMobile.value) sidebarOpen.value = false
  }
}

// ===== Mermaid Diagram Definitions =====

const overviewChart = `flowchart LR
  subgraph Sensors["Wearable Sensors"]
    EMG["EMG Sensor"]
    HR["HR / SpO2 Sensor"]
    IMU["IMU Sensor"]
  end
  subgraph ESP["ESP32 Edge Processing"]
    RTOS["FreeRTOS Tasks"]
    TFL["TFLite Autoencoder"]
  end
  subgraph Dash["Caregiver Dashboard"]
    VUE["Vue.js + Chart.js"]
  end
  Sensors -->|Analog + I2C| RTOS
  RTOS -->|PhysioState| TFL
  TFL -->|Anomaly| Haptic["Haptic Motor"]
  TFL -->|WiFi SSE| VUE`

const i2cChart = `flowchart TB
  ESP["YD-ESP32 Type-A"]
  ESP -->|SDA GPIO 21| BUS["I2C Bus"]
  ESP -->|SCL GPIO 22| BUS
  BUS --> MAX["MAX30102 PPG\nAddress: 0x57"]
  BUS --> MPU["MPU-9250\nAddress: 0x68"]
  BUS --> OLED["SH1106 OLED\nAddress: 0x3C"]
  ESP -->|Analog GPIO 34| EMG["OYMotion SEN0240\nEMG Sensor"]
  ESP -->|LEDC PWM GPIO 25| LRA["LRA Haptic Motor"]`

const firmwareChart = `flowchart TB
  subgraph Core0["Core 0"]
    EMGTask["EMG Task\nPriority 3, 1000 Hz"]
  end
  subgraph Core1["Core 1"]
    SensorTask["Sensor Task\nPriority 2, 25 Hz"]
    DisplayTask["Display Task\nPriority 1, 2 Hz"]
    SerialTask["Serial Task\nPriority 1, 1 Hz"]
    InferenceTask["Inference Task\nPriority 1, 0.5 Hz"]
    HTTPTask["HTTP Task\nPriority 2, Event-driven"]
  end

  EMGTask -->|"emg_envelope"| Shared["PhysioState\nMutex Protected"]
  SensorTask -->|"hr, spo2, motion"| Shared
  Shared --> DisplayTask
  Shared --> SerialTask
  Shared --> InferenceTask
  InferenceTask -->|"mse, anomaly"| HTTPTask
  Shared -->|"anomaly"| HapticOut["Haptic Motor\nGPIO 25"]`

const emgChart = `flowchart LR
  ADC["Raw ADC\n12-bit, 1000 Hz"] --> Filters["EMGFilters\nHPF 20 Hz\nLPF 150 Hz\nNotch 50 Hz"]
  Filters --> Rect["Rectified\nSquared Envelope"]
  Rect --> EMA["EMA Smoothing\nα = 0.01"]
  EMA --> Output["PhysioState\n.emg_envelope"]`

const mlPipelineChart = `flowchart LR
  Capture["Baseline Capture\npyserial CSV"] --> Clean["Data Cleaning"]
  Clean --> Norm["Z-Normalization"]
  Norm --> Train["Autoencoder Training\nKeras / TensorFlow"]
  Train --> Threshold["Threshold Calibration\nmean + 3σ"]
  Threshold --> Export["TFLite Export"]
  Export --> Header["C Header Array"]
  Header --> Flash["ESP32 Flash"]`

const autoencoderChart = `flowchart LR
  Input["Input Layer\n3 neurons"] --> D1["Dense 16\nReLU"]
  D1 --> D2["Dense 8\nReLU Latent"]
  D2 --> D3["Dense 16\nReLU"]
  D3 --> Output["Output Layer\n3 neurons Linear"]`

const anomalyChart = `flowchart TD
  Start["New MSE Value"] --> Check{"MSE > Threshold?"}
  Check -->|No| Reset["Reset Counter"]
  Check -->|Yes| Inc["Increment Counter"]
  Inc --> Confirm{"Counter ≥ 3?"}
  Confirm -->|No| Wait["Continue Sampling"]
  Confirm -->|Yes| Window["Open 5s Window"]
  Window --> WindowCheck{"Continuous anomaly\nfor 5s OR 3+ events?"}
  WindowCheck -->|No| Wait
  WindowCheck -->|Yes| Alert["TRIGGER ALERT"]
  Alert --> Haptic["Fire Haptic Motor"]
  Alert --> SSE["Set SSE anomaly flag"]
  SSE --> Cooldown["15s Cooldown"]
  Cooldown --> Reset
  Haptic --> Cooldown`

const sseChart = `sequenceDiagram
  participant Browser
  participant ESP32

  Browser->>ESP32: GET /events
  ESP32-->>Browser: SSE headers (200 OK)

  loop Every 1 second
    ESP32-->>Browser: data: {"hr":72,...}
  end

  loop Every 3 seconds
    ESP32-->>Browser: : keepalive
  end

  Browser->>ESP32: GET /telemetry
  ESP32-->>Browser: {"hr":72,"spo2":98,...}
  Note over ESP32,Browser: Connection closes after response`
</script>

<style scoped>
/* ===== Page Container ===== */
.docs-page {
  display: flex;
  flex-direction: column;
  height: 100%;
  overflow: hidden;
  background: var(--bg-primary);
}

/* ===== Layout ===== */
.docs-layout {
  display: flex;
  max-width: 1280px;
  width: 100%;
  margin: 0 auto;
  flex: 1;
  overflow: hidden;
}

/* ===== Sidebar ===== */
.docs-sidebar {
  width: 240px;
  flex-shrink: 0;
  padding: 1.5rem 0 1.5rem 2rem;
  overflow-y: auto;
  border-right: 1px solid var(--border);
  background: var(--card-bg);
}

.sidebar-brand {
  margin-bottom: 1.25rem;
}

.sidebar-brand-link {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  text-decoration: none;
  padding: 0.4rem 0.6rem;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  background: rgba(255, 255, 255, 0.03);
  transition: all var(--transition-fast);
}

.sidebar-brand-link:hover {
  border-color: var(--border-hover);
  background: rgba(255, 255, 255, 0.06);
}

.sidebar-brand-link:hover .sidebar-home-icon {
  color: var(--accent);
}

.sidebar-logo {
  height: 22px;
  width: auto;
  object-fit: contain;
}

.sidebar-brand-name {
  font-size: 0.95rem;
  font-weight: 700;
  color: var(--text-primary);
  flex: 1;
}

.sidebar-brand-link:hover .sidebar-brand-name {
  color: var(--accent);
}

.sidebar-home-icon {
  color: var(--text-muted);
  flex-shrink: 0;
  transition: color var(--transition-fast);
}

.sidebar-home-hint {
  font-size: 0.65rem;
  color: var(--text-muted);
  margin: 0.3rem 0 0 0.6rem;
}

.sidebar-toggle {
  display: none;
  width: 100%;
  align-items: center;
  gap: 0.5rem;
  padding: 0.6rem 1rem;
  background: none;
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  font-family: inherit;
  font-size: 0.85rem;
  font-weight: 600;
  color: var(--text-primary);
  cursor: pointer;
  transition: all var(--transition-fast);
}

.sidebar-toggle:hover {
  background: var(--card-bg-hover);
  border-color: var(--border-hover);
}

.toggle-chevron {
  margin-left: auto;
  transition: transform 0.2s ease;
}

.toggle-chevron.rotated {
  transform: rotate(180deg);
}

.toc-list {
  list-style: none;
  margin: 0;
  padding: 0;
}

.toc-list li {
  margin-bottom: 0.2rem;
}

.toc-list a {
  display: block;
  padding: 0.4rem 0.75rem;
  font-size: 0.82rem;
  font-weight: 500;
  color: var(--text-secondary);
  text-decoration: none;
  border-radius: var(--radius-sm);
  border-left: 2px solid transparent;
  transition: all var(--transition-fast);
  line-height: 1.4;
}

.toc-list a:hover {
  color: var(--text-primary);
  background: var(--card-bg-hover);
}

.toc-list a.active {
  color: var(--accent);
  background: var(--accent-glow);
  border-left-color: var(--accent);
  font-weight: 600;
}

/* ===== Main Content ===== */
.docs-main {
  flex: 1;
  padding: 2rem 2.5rem 4rem;
  min-width: 0;
  overflow-y: auto;
}

/* ===== Section Cards ===== */
.doc-section {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  padding: 2rem;
  margin-bottom: 1.5rem;
  box-shadow: var(--shadow-sm);
  scroll-margin-top: 72px;
}

.doc-section-alt {
  background: rgba(255, 255, 255, 0.01);
  border-radius: var(--radius-md);
}

.image-grid {
  display: grid;
  gap: 0.75rem;
  margin-top: 1rem;
}

.image-grid.three-col {
  grid-template-columns: repeat(3, 1fr);
}

.image-grid.two-col {
  grid-template-columns: repeat(2, 1fr);
}

.doc-image {
  width: 100%;
  height: 200px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  object-fit: cover;
  transition: border-color var(--transition-fast);
 cursor: pointer;
}

.doc-image:hover {
  border-color: var(--border-hover);
}

.doc-image-single {
  margin-top: 1rem;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  overflow: hidden;
}

.doc-image-single img {
  width: 100%;
  max-height: 360px;
  object-fit: cover;
  display: block;
}

@media (max-width: 900px) {
  .image-grid.three-col {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 640px) {
  .image-grid.three-col,
  .image-grid.two-col {
    grid-template-columns: 1fr;
  }
}

.section-label {
  font-size: 0.72rem;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 1.5px;
  color: var(--accent);
  margin-bottom: 0.5rem;
  display: inline-flex;
  align-items: center;
  gap: 0.6rem;
  padding-bottom: 0.35rem;
  border-bottom: 2px solid rgba(139, 92, 246, 0.3);
}

.section-label::before {
  content: '';
  width: 3px;
  height: 24px;
  background: linear-gradient(to bottom, var(--accent), var(--accent-cyan));
  border-radius: 2px;
  flex-shrink: 0;
}

.section-heading {
  font-size: 1.6rem;
  font-weight: 700;
  color: var(--text-primary);
  letter-spacing: -0.02em;
  margin: 0 0 1rem;
  line-height: 1.25;
}

.sub-heading {
  font-size: 1.1rem;
  font-weight: 600;
  color: var(--text-primary);
  margin: 1.5rem 0 0.75rem;
  letter-spacing: -0.01em;
}

.section-text {
  font-size: 0.92rem;
  color: var(--text-secondary);
  line-height: 1.75;
  margin-bottom: 1rem;
}

.section-text:last-child {
  margin-bottom: 0;
}

.section-text code {
  background: var(--accent-glow);
  color: var(--accent);
  padding: 0.15rem 0.4rem;
  border-radius: 4px;
  font-size: 0.85em;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

.doc-callout {
  background: rgba(139, 92, 246, 0.06);
  border-left: 3px solid var(--accent);
  border-radius: 0 var(--radius-sm) var(--radius-sm) 0;
  padding: 0.75rem 1rem;
  margin: 1rem 0;
  font-size: 0.88rem;
  line-height: 1.65;
  color: var(--text-secondary);
}

/* ===== Tables ===== */
.table-wrap {
  overflow-x: auto;
  margin: 1rem 0;
}

.doc-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 0.85rem;
}

.doc-table th {
  background: var(--bg-secondary);
  font-weight: 600;
  color: var(--text-primary);
  padding: 0.6rem 0.75rem;
  text-align: left;
  border-bottom: 2px solid var(--border);
  font-size: 0.8rem;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  white-space: nowrap;
}

.doc-table td {
  padding: 0.55rem 0.75rem;
  border-bottom: 1px solid var(--border);
  color: var(--text-secondary);
  vertical-align: top;
}

.doc-table td code {
  background: var(--accent-glow);
  color: var(--accent);
  padding: 0.1rem 0.35rem;
  border-radius: 4px;
  font-size: 0.82em;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
}

.doc-table tbody tr:hover {
  background: rgba(255, 255, 255, 0.03);
}

/* ===== Code Blocks ===== */
.code-block {
  background: #0D0D1A;
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: var(--radius-sm);
  padding: 1rem 1.25rem;
  margin: 1rem 0;
  overflow-x: auto;
}

.code-block pre {
  margin: 0;
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-size: 0.85rem;
  line-height: 1.6;
  color: #E0E0F0;
}

.code-kw { color: #C084FC; }
.code-type { color: #67E8F9; }
.code-fn { color: #60A5FA; }
.code-key { color: #FF7A00; }
.code-num { color: #00E5A0; }

/* ===== Spec Grid ===== */
.spec-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  gap: 0.75rem;
  margin: 1rem 0;
}

.spec-item {
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  padding: 0.75rem 1rem;
  display: flex;
  flex-direction: column;
  gap: 0.2rem;
  transition: all var(--transition-fast);
}

.spec-item:hover {
  border-color: var(--border-hover);
  background: rgba(255, 255, 255, 0.02);
}

.spec-label {
  font-size: 0.72rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.8px;
  color: var(--text-muted);
}

.spec-value {
  font-size: 0.92rem;
  font-weight: 600;
  color: var(--text-primary);
}

/* ===== Feature Grid ===== */
.feature-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 1rem;
  margin: 1.25rem 0;
}

.feature-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  padding: 1.25rem;
  transition: all var(--transition-fast);
}

.feature-card:hover {
  border-color: var(--border-hover);
  box-shadow: var(--shadow-sm);
}

.feature-title {
  font-size: 0.95rem;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0 0 0.4rem;
}

.feature-desc {
  font-size: 0.84rem;
  color: var(--text-secondary);
  line-height: 1.6;
  margin: 0;
}

/* ===== Responsive ===== */
@media (max-width: 900px) {
  .docs-layout {
    flex-direction: column;
    overflow-y: auto;
  }

  .docs-sidebar {
    width: 100%;
    border-right: none;
    border-bottom: 1px solid var(--border);
    padding: 0.75rem 1.5rem;
  }

  .sidebar-toggle {
    display: flex;
  }

  .docs-main {
    padding: 1.5rem;
  }

  .feature-grid {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 640px) {
  .docs-main {
    padding: 1rem;
  }

  .doc-section {
    padding: 1.25rem;
  }

  .spec-grid {
    grid-template-columns: 1fr;
  }
}
</style>
