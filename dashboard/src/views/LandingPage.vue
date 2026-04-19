<template>
  <div class="landing">
    <!-- HTTP Compatibility Warning -->
    <div v-if="!httpSupported" class="compat-warning">
      <AlertTriangle :size="16" />
      <span>Your browser does not support HTTP fetch. The dashboard will not work. Please use <strong>Chrome</strong>, <strong>Edge</strong>, <strong>Firefox</strong>, or <strong>Safari</strong>.</span>
    </div>

    <!-- ===== HERO ===== -->
    <section class="hero" id="hero">
      <div class="hero-bg">
        <div class="orb orb-1" />
        <div class="orb orb-2" />
        <div class="orb orb-3" />
        <div class="orb orb-4" />
      </div>

      <nav class="hero-nav">
        <router-link to="/" class="nav-brand">Senseware</router-link>
        <div class="nav-links">
          <a href="#about">About</a>
          <a href="#how-it-works">How It Works</a>
          <a href="#tech">Technology</a>
          <router-link to="/dashboard" class="nav-cta">Dashboard</router-link>
        </div>
      </nav>

      <div class="hero-content">
        <div class="reveal">
          <p class="hero-badge">Edge-AI Wearable</p>
          <h1 class="hero-title">
            <span class="gradient-text">Senseware</span>
          </h1>
          <p class="hero-subtitle">Edge-AI Wearable Stress Detection for Autism Care</p>
          <p class="hero-tagline">
            A compassionate wearable system that learns your calm baseline and detects
            physiological stress in real-time — empowering caregivers with the visibility
            they need for timely, gentle intervention.
          </p>
          <div class="hero-actions">
            <router-link to="/dashboard" class="btn btn-primary">
              <MonitorSmartphone :size="18" />
              Open Dashboard
            </router-link>
            <a href="#about" class="btn btn-outline">
              <ArrowDown :size="18" />
              Learn More
            </a>
          </div>
        </div>
      </div>

      <div class="hero-scroll-hint">
        <ChevronDown :size="20" class="bounce" />
      </div>
    </section>

    <!-- ===== ABOUT / PROBLEM STATEMENT ===== -->
    <section id="about" class="section about-section">
      <div class="section-inner">
        <div class="about-grid">
          <div class="about-image reveal">
            <div class="image-placeholder">
              <Camera :size="32" />
              <span>Image Placeholder</span>
            </div>
          </div>
          <div class="about-text reveal">
            <p class="section-label">Understanding the Challenge</p>
            <h2 class="section-heading">Every moment of distress<br />deserves to be seen</h2>
            <p>
              Individuals on the autism spectrum often experience heightened sensitivity
              to sensory input and emotional stress, yet many face profound difficulty
              communicating their discomfort or distress to those around them. This silent
              struggle can lead to escalating meltdowns that might have been prevented with
              earlier awareness.
            </p>
            <p>
              Current stress monitoring solutions require expensive clinical-grade equipment,
              specialized training, or invasive setups that are impractical for everyday life.
              Caregivers — whether parents, teachers, or support workers — are left to rely
              on behavioral observation alone, often detecting distress only after it has
              already escalated.
            </p>
            <p>
              <strong class="text-accent">Senseware changes this.</strong> By combining
              wearable biosensors with on-device machine learning, it provides a continuous,
              non-invasive window into physiological state — giving caregivers the real-time
              insight they need to offer support before a crisis develops.
            </p>
          </div>
        </div>
      </div>
    </section>

    <!-- ===== HOW IT WORKS ===== -->
    <section id="how-it-works" class="section works-section">
      <div class="section-inner">
        <div class="reveal">
          <p class="section-label">How It Works</p>
          <h2 class="section-heading">Three steps to calmer care</h2>
          <p class="section-desc">
            Senseware operates on a closed-loop system that continuously monitors, learns, and alerts.
          </p>
        </div>

        <div class="steps-grid">
          <div class="step-card reveal" v-for="(step, i) in steps" :key="i">
            <div class="step-icon-wrap" :class="step.colorClass">
              <component :is="step.icon" :size="28" />
            </div>
            <div class="step-number">{{ String(i + 1).padStart(2, '0') }}</div>
            <h3 class="step-title">{{ step.title }}</h3>
            <p class="step-desc">{{ step.desc }}</p>
          </div>
        </div>
      </div>
    </section>

    <!-- ===== TECHNOLOGY STACK ===== -->
    <section id="tech" class="section tech-section">
      <div class="section-inner">
        <div class="reveal">
          <p class="section-label">Built with Purpose</p>
          <h2 class="section-heading">Purpose-driven technology</h2>
          <p class="section-desc">
            Every component was chosen for reliability, efficiency, and the ability to run
            intelligently at the edge — no cloud, no latency, no privacy compromise.
          </p>
        </div>

        <div class="tech-grid">
          <div class="tech-card reveal" v-for="(tech, i) in techStack" :key="i">
            <component :is="tech.icon" :size="24" class="tech-icon" />
            <h4 class="tech-name">{{ tech.name }}</h4>
            <p class="tech-desc">{{ tech.desc }}</p>
          </div>
        </div>
      </div>
    </section>

    <!-- ===== TEAM ===== -->
    <section class="section team-section">
      <div class="section-inner">
        <div class="reveal">
          <p class="section-label">Our Team</p>
          <h2 class="section-heading">The people behind Senseware</h2>
        </div>

        <div class="team-grid">
          <div class="team-card reveal" v-for="(member, i) in team" :key="i">
            <div class="team-avatar" :style="{ background: member.color }">
              {{ member.initials }}
            </div>
            <h4 class="team-name">{{ member.name }}</h4>
            <p class="team-role">{{ member.role }}</p>
          </div>
        </div>
      </div>
    </section>

    <!-- ===== FOOTER ===== -->
    <footer class="landing-footer">
      <div class="footer-inner">
        <div class="footer-brand">
          <span class="gradient-text">Senseware</span>
          <p class="footer-tagline">Edge-AI Wearable Stress Detection</p>
        </div>
        <div class="footer-links">
          <router-link to="/dashboard">Dashboard</router-link>
          <a href="#about">About</a>
          <a href="https://github.com" target="_blank" rel="noopener">GitHub</a>
        </div>
        <p class="footer-copy">&copy; 2026 Senseware &mdash; SUTD</p>
      </div>
    </footer>
  </div>
</template>

<script setup>
import { onMounted, onUnmounted, ref } from 'vue'
import {
  MonitorSmartphone,
  ArrowDown,
  ChevronDown,
  Camera,
  Activity,
  Brain,
  Bell,
  Cpu,
  Wifi,
  Layers,
  Clock,
  Code2,
  AlertTriangle,
} from 'lucide-vue-next'

// Steps data
const steps = [
  {
    icon: Activity,
    title: 'Sense',
    desc: 'Wearable sensors continuously monitor heart rate (PPG), muscle tension (EMG), and motion patterns (IMU) — all sampled at high frequency for clinical-grade fidelity.',
    colorClass: 'step-sense',
  },
  {
    icon: Brain,
    title: 'Detect',
    desc: 'A lightweight autoencoder running natively on the ESP32 learns your personal calm baseline. When physiological patterns deviate, it detects stress anomalies in real-time — no cloud needed.',
    colorClass: 'step-detect',
  },
  {
    icon: Bell,
    title: 'Alert',
    desc: 'Caregivers receive instant alerts on any phone or tablet via HTTP. The companion dashboard visualizes live vitals and logs every intervention, enabling data-informed care decisions.',
    colorClass: 'step-alert',
  },
]

// Tech stack data
const techStack = [
  {
    icon: Cpu,
    name: 'ESP32',
    desc: 'Low-power microcontroller running the entire ML pipeline at the edge',
  },
  {
    icon: Brain,
    name: 'TensorFlow Lite',
    desc: 'On-device autoencoder inference for real-time anomaly detection',
  },
  {
    icon: Wifi,
    name: 'HTTP',
    desc: 'Real-time Server-Sent Events push from ESP32 — no app required',
  },
  {
    icon: Layers,
    name: 'Autoencoder ML',
    desc: 'Learns personal calm baselines and flags physiological deviations',
  },
  {
    icon: Clock,
    name: 'FreeRTOS',
    desc: 'Deterministic real-time OS for reliable sensor sampling',
  },
  {
    icon: Code2,
    name: 'Vue.js',
    desc: 'Lightweight reactive UI for the caregiver dashboard',
  },
]

// Team placeholders
const team = [
  { name: 'Alex Chen', role: 'Embedded Systems', initials: 'AC', color: 'linear-gradient(135deg, #e8913a, #e85d4a)' },
  { name: 'Sarah Lim', role: 'Machine Learning', initials: 'SL', color: 'linear-gradient(135deg, #5a9ec7, #b08adb)' },
  { name: 'Jordan Ng', role: 'UX / Design', initials: 'JN', color: 'linear-gradient(135deg, #5ab88f, #5a9ec7)' },
  { name: 'Priya Patel', role: 'Clinical Research', initials: 'PP', color: 'linear-gradient(135deg, #e8b13a, #e8913a)' },
]

// ===== Scroll Reveal (IntersectionObserver) =====
// HTTP browser support check
const httpSupported = ref(true)

let observer = null

onMounted(() => {
  httpSupported.value = typeof window !== 'undefined' && 'fetch' in window

  observer = new IntersectionObserver(
    (entries) => {
      entries.forEach((entry) => {
        if (entry.isIntersecting) {
          entry.target.classList.add('revealed')
          observer.unobserve(entry.target)
        }
      })
    },
    { threshold: 0.12, rootMargin: '0px 0px -40px 0px' }
  )

  document.querySelectorAll('.reveal').forEach((el) => observer.observe(el))
})

onUnmounted(() => {
  if (observer) observer.disconnect()
})
</script>

<style scoped>
/* ===== LANDING CONTAINER ===== */
.landing {
  overflow-x: hidden;
}

/* ===== BROWSER COMPATIBILITY WARNING ===== */
.compat-warning {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.7rem 1.5rem;
  background: rgba(232, 93, 74, 0.12);
  border-bottom: 1px solid rgba(232, 93, 74, 0.3);
  color: #f0a09a;
  font-size: 0.82rem;
  line-height: 1.4;
  text-align: center;
  justify-content: center;
  position: relative;
  z-index: 20;
}

.compat-warning strong {
  color: #f5f0eb;
}

/* ===== HERO ===== */
.hero {
  position: relative;
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: center;
  text-align: center;
  padding: 2rem 1.5rem 3rem;
  overflow: hidden;
  background: radial-gradient(ellipse at 50% 30%, #2d261f 0%, #1a1614 70%);
}

.hero-bg {
  position: absolute;
  inset: 0;
  overflow: hidden;
  pointer-events: none;
}

.orb {
  position: absolute;
  border-radius: 50%;
  filter: blur(80px);
  opacity: 0.35;
}

.orb-1 {
  width: 400px;
  height: 400px;
  background: radial-gradient(circle, rgba(232, 145, 58, 0.4), transparent 70%);
  top: -100px;
  right: -100px;
  animation: drift-1 20s ease-in-out infinite;
}

.orb-2 {
  width: 300px;
  height: 300px;
  background: radial-gradient(circle, rgba(90, 158, 199, 0.3), transparent 70%);
  bottom: 10%;
  left: -80px;
  animation: drift-2 25s ease-in-out infinite;
}

.orb-3 {
  width: 250px;
  height: 250px;
  background: radial-gradient(circle, rgba(176, 138, 219, 0.25), transparent 70%);
  top: 40%;
  right: 15%;
  animation: drift-3 18s ease-in-out infinite;
}

.orb-4 {
  width: 200px;
  height: 200px;
  background: radial-gradient(circle, rgba(232, 177, 58, 0.2), transparent 70%);
  bottom: 30%;
  left: 30%;
  animation: drift-4 22s ease-in-out infinite;
}

@keyframes drift-1 {
  0%, 100% { transform: translate(0, 0) scale(1); }
  33% { transform: translate(-60px, 40px) scale(1.1); }
  66% { transform: translate(30px, -30px) scale(0.95); }
}

@keyframes drift-2 {
  0%, 100% { transform: translate(0, 0) scale(1); }
  33% { transform: translate(50px, -40px) scale(1.05); }
  66% { transform: translate(-20px, 50px) scale(0.9); }
}

@keyframes drift-3 {
  0%, 100% { transform: translate(0, 0); }
  50% { transform: translate(-40px, -30px); }
}

@keyframes drift-4 {
  0%, 100% { transform: translate(0, 0); }
  50% { transform: translate(35px, 25px); }
}

/* Nav */
.hero-nav {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 1.25rem 2rem;
  z-index: 10;
}

.nav-brand {
  font-size: 1.25rem;
  font-weight: 700;
  color: var(--text-primary);
  letter-spacing: -0.02em;
}

.nav-links {
  display: flex;
  align-items: center;
  gap: 1.75rem;
}

.nav-links a {
  font-size: 0.88rem;
  font-weight: 500;
  color: var(--text-secondary);
  transition: color var(--transition-fast);
}

.nav-links a:hover {
  color: var(--text-primary);
}

.nav-cta {
  background: var(--accent);
  color: var(--bg-primary) !important;
  padding: 0.45rem 1rem;
  border-radius: var(--radius-sm);
  font-weight: 600 !important;
  transition: background var(--transition-fast);
}

.nav-cta:hover {
  background: var(--accent-hover) !important;
}

/* Hero content */
.hero-content {
  max-width: 680px;
  z-index: 5;
}

.hero-badge {
  display: inline-block;
  padding: 0.35rem 1rem;
  border-radius: 100px;
  border: 1px solid var(--border);
  background: var(--card-bg);
  color: var(--accent);
  font-size: 0.78rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 1px;
  margin-bottom: 1.5rem;
}

.hero-title {
  font-size: clamp(3rem, 8vw, 5.5rem);
  font-weight: 800;
  line-height: 1.05;
  letter-spacing: -0.03em;
  margin-bottom: 1rem;
}

.gradient-text {
  background: linear-gradient(135deg, #e8b13a 0%, #e8913a 40%, #e85d4a 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.hero-subtitle {
  font-size: clamp(1.05rem, 2.5vw, 1.35rem);
  font-weight: 500;
  color: var(--text-secondary);
  margin-bottom: 1rem;
}

.hero-tagline {
  font-size: 1rem;
  line-height: 1.7;
  color: var(--text-muted);
  max-width: 540px;
  margin: 0 auto 2rem;
}

.hero-actions {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 1rem;
  flex-wrap: wrap;
}

/* Buttons */
.btn {
  display: inline-flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.75rem 1.5rem;
  border-radius: var(--radius-sm);
  font-size: 0.95rem;
  font-weight: 600;
  cursor: pointer;
  transition: all var(--transition-normal);
  border: none;
  text-decoration: none;
}

.btn-primary {
  background: var(--accent);
  color: var(--bg-primary);
}

.btn-primary:hover {
  background: var(--accent-hover);
  transform: translateY(-2px);
  box-shadow: 0 8px 24px rgba(232, 145, 58, 0.3);
}

.btn-outline {
  background: transparent;
  border: 1px solid var(--border);
  color: var(--text-secondary);
}

.btn-outline:hover {
  border-color: var(--text-secondary);
  color: var(--text-primary);
  transform: translateY(-2px);
}

/* Scroll hint */
.hero-scroll-hint {
  position: absolute;
  bottom: 2rem;
  color: var(--text-muted);
  opacity: 0.5;
}

.bounce {
  animation: gentle-bounce 2s ease-in-out infinite;
}

@keyframes gentle-bounce {
  0%, 100% { transform: translateY(0); }
  50% { transform: translateY(6px); }
}

/* ===== SHARED SECTION STYLES ===== */
.section {
  padding: 6rem 1.5rem;
}

.section-inner {
  max-width: 1100px;
  margin: 0 auto;
}

.section-label {
  font-size: 0.78rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 1.5px;
  color: var(--accent);
  margin-bottom: 0.75rem;
}

.section-heading {
  font-size: clamp(1.75rem, 4vw, 2.5rem);
  font-weight: 700;
  letter-spacing: -0.02em;
  line-height: 1.2;
  margin-bottom: 1rem;
}

.section-desc {
  font-size: 1.05rem;
  color: var(--text-secondary);
  max-width: 560px;
  line-height: 1.7;
}

.text-accent {
  color: var(--accent);
}

/* ===== ABOUT ===== */
.about-section {
  background: var(--bg-secondary);
}

.about-grid {
  display: grid;
  grid-template-columns: 1fr 1.2fr;
  gap: 3rem;
  align-items: center;
}

.image-placeholder {
  width: 100%;
  aspect-ratio: 4 / 3;
  border: 2px dashed var(--border);
  border-radius: var(--radius-lg);
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 0.75rem;
  color: var(--text-muted);
  font-size: 0.9rem;
  background: var(--card-bg);
}

.about-text p {
  color: var(--text-secondary);
  margin-bottom: 1rem;
  font-size: 0.95rem;
  line-height: 1.75;
}

/* ===== HOW IT WORKS ===== */
.works-section {
  background: var(--bg-primary);
}

.steps-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 1.5rem;
  margin-top: 3rem;
}

.step-card {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: var(--radius-lg);
  padding: 2rem 1.5rem;
  transition: all var(--transition-normal);
  position: relative;
  overflow: hidden;
}

.step-card::before {
  content: '';
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 3px;
  border-radius: 3px 3px 0 0;
  transition: opacity var(--transition-normal);
  opacity: 0.7;
}

.step-sense::before { background: var(--color-danger); }
.step-detect::before { background: var(--color-info); }
.step-alert::before { background: var(--color-warning); }

.step-card:hover {
  transform: translateY(-4px);
  border-color: var(--border-hover);
  box-shadow: var(--shadow-lg);
}

.step-card:hover::before {
  opacity: 1;
}

.step-icon-wrap {
  width: 52px;
  height: 52px;
  border-radius: var(--radius-md);
  display: flex;
  align-items: center;
  justify-content: center;
  margin-bottom: 1.25rem;
}

.step-sense .step-icon-wrap {
  background: rgba(232, 93, 74, 0.12);
  color: var(--color-danger);
}

.step-detect .step-icon-wrap {
  background: rgba(90, 158, 199, 0.12);
  color: var(--color-info);
}

.step-alert .step-icon-wrap {
  background: rgba(232, 177, 58, 0.12);
  color: var(--color-warning);
}

.step-number {
  font-size: 0.72rem;
  font-weight: 700;
  color: var(--text-muted);
  text-transform: uppercase;
  letter-spacing: 1px;
  margin-bottom: 0.5rem;
}

.step-title {
  font-size: 1.2rem;
  font-weight: 700;
  margin-bottom: 0.75rem;
  letter-spacing: -0.01em;
}

.step-desc {
  font-size: 0.9rem;
  color: var(--text-secondary);
  line-height: 1.65;
}

/* ===== TECHNOLOGY ===== */
.tech-section {
  background: var(--bg-secondary);
}

.tech-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 1rem;
  margin-top: 3rem;
}

.tech-card {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  padding: 1.25rem;
  transition: all var(--transition-normal);
}

.tech-card:hover {
  border-color: var(--border-hover);
  transform: translateY(-2px);
}

.tech-icon {
  color: var(--accent);
  margin-bottom: 0.75rem;
}

.tech-name {
  font-size: 0.95rem;
  font-weight: 600;
  margin-bottom: 0.35rem;
}

.tech-desc {
  font-size: 0.8rem;
  color: var(--text-muted);
  line-height: 1.5;
}

/* ===== TEAM ===== */
.team-section {
  background: var(--bg-primary);
}

.team-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 1.5rem;
  margin-top: 3rem;
}

.team-card {
  text-align: center;
  padding: 1.5rem 1rem;
}

.team-avatar {
  width: 72px;
  height: 72px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  margin: 0 auto 1rem;
  font-size: 1.1rem;
  font-weight: 700;
  color: var(--bg-primary);
  letter-spacing: 1px;
}

.team-name {
  font-size: 0.95rem;
  font-weight: 600;
  margin-bottom: 0.25rem;
}

.team-role {
  font-size: 0.8rem;
  color: var(--text-muted);
}

/* ===== FOOTER ===== */
.landing-footer {
  border-top: 1px solid var(--border);
  padding: 3rem 1.5rem;
}

.footer-inner {
  max-width: 1100px;
  margin: 0 auto;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 1.5rem;
}

.footer-brand {
  text-align: center;
}

.footer-brand .gradient-text {
  font-size: 1.15rem;
  font-weight: 700;
}

.footer-tagline {
  font-size: 0.82rem;
  color: var(--text-muted);
  margin-top: 0.25rem;
}

.footer-links {
  display: flex;
  gap: 2rem;
}

.footer-links a {
  font-size: 0.85rem;
  color: var(--text-secondary);
  transition: color var(--transition-fast);
}

.footer-links a:hover {
  color: var(--text-primary);
}

.footer-copy {
  font-size: 0.75rem;
  color: var(--text-muted);
}

/* ===== SCROLL REVEAL ===== */
.reveal {
  opacity: 0;
  transform: translateY(28px);
  transition: opacity 0.7s ease, transform 0.7s ease;
}

.reveal.revealed {
  opacity: 1;
  transform: translateY(0);
}

/* Stagger children within grids */
.steps-grid .reveal:nth-child(2),
.tech-grid .reveal:nth-child(2) {
  transition-delay: 0.1s;
}

.steps-grid .reveal:nth-child(3),
.tech-grid .reveal:nth-child(3) {
  transition-delay: 0.2s;
}

.tech-grid .reveal:nth-child(4) {
  transition-delay: 0.1s;
}

.tech-grid .reveal:nth-child(5) {
  transition-delay: 0.2s;
}

.tech-grid .reveal:nth-child(6) {
  transition-delay: 0.3s;
}

.team-grid .reveal:nth-child(2) {
  transition-delay: 0.1s;
}

.team-grid .reveal:nth-child(3) {
  transition-delay: 0.2s;
}

.team-grid .reveal:nth-child(4) {
  transition-delay: 0.3s;
}

/* ===== RESPONSIVE ===== */
@media (max-width: 900px) {
  .about-grid {
    grid-template-columns: 1fr;
    gap: 2rem;
  }

  .steps-grid {
    grid-template-columns: 1fr;
  }

  .tech-grid {
    grid-template-columns: repeat(2, 1fr);
  }

  .team-grid {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 640px) {
  .hero-nav {
    flex-direction: column;
    gap: 1rem;
    padding: 1rem;
  }

  .nav-links {
    gap: 1rem;
  }

  .hero {
    padding: 5rem 1.25rem 3rem;
  }

  .section {
    padding: 4rem 1.25rem;
  }

  .tech-grid {
    grid-template-columns: 1fr;
  }

  .team-grid {
    grid-template-columns: 1fr 1fr;
  }

  .hero-actions {
    flex-direction: column;
  }

  .hero-actions .btn {
    width: 100%;
    justify-content: center;
  }
}
</style>
