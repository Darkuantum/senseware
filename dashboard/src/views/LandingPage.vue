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
        <div v-if="heroImages.length > 0" class="hero-slider">
          <img
            v-for="(img, i) in heroImages"
            :key="i"
            :src="img"
            :class="['hero-slide', { active: currentSlide === i }]"
            alt="Senseware wearable stress detection device"
          />
        </div>
        <div v-if="heroImages.length > 0" class="hero-overlay" />
        <div class="orb orb-1" />
        <div class="orb orb-2" />
        <div class="orb orb-3" />
        <div class="orb orb-4" />
      </div>

      <div v-if="heroImages.length > 1" class="hero-dots">
        <button
          v-for="(_, i) in heroImages"
          :key="i"
          :class="['dot', { active: currentSlide === i }]"
          @click="goToSlide(i)"
          :aria-label="`Slide ${i + 1}`"
        />
      </div>

      <nav class="hero-nav">
        <router-link to="/" class="nav-brand">
            <img src="/logo.png" alt="Senseware" class="nav-logo" />
          </router-link>
        <div class="nav-links">
          <a href="#about">About</a>
          <a href="#how-it-works">How It Works</a>
          <a href="#tech">Technology</a>
          <router-link to="/docs">Docs</router-link>
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
            <div class="about-visual">
              <div class="about-visual-pulse"></div>
              <div class="about-visual-pulse about-visual-pulse-2"></div>
              <div class="about-visual-icon">
                <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"/></svg>
              </div>
              <p class="about-visual-label">Non-invasive wearable sensor</p>
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
              <Activity v-if="step.iconName === 'Activity'" :size="28" />
              <Brain v-else-if="step.iconName === 'Brain'" :size="28" />
              <Bell v-else :size="28" />
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
            <Cpu v-if="tech.iconName === 'Cpu'" :size="24" class="tech-icon" />
            <Brain v-else-if="tech.iconName === 'Brain'" :size="24" class="tech-icon" />
            <Wifi v-else-if="tech.iconName === 'Wifi'" :size="24" class="tech-icon" />
            <Layers v-else-if="tech.iconName === 'Layers'" :size="24" class="tech-icon" />
            <Clock v-else-if="tech.iconName === 'Clock'" :size="24" class="tech-icon" />
            <Code2 v-else :size="24" class="tech-icon" />
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
          <router-link to="/docs">Docs</router-link>
          <a href="#about">About</a>
          <a href="https://github.com/Darkuantum/senseware" target="_blank" rel="noopener">GitHub</a>
        </div>
        <p class="footer-copy">&copy; 2026 Senseware &mdash; SUTD</p>
      </div>
    </footer>
  </div>
</template>

<script setup>
import { onMounted, onUnmounted, ref, computed } from 'vue'
import {
  MonitorSmartphone,
  ArrowDown,
  ChevronDown,
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

const heroImageModules = import.meta.glob('/public/images/hero/*.{jpg,jpeg,png,webp}', { eager: true })
const heroImages = Object.keys(heroImageModules)
  .sort()
  .map((path) => path.replace('/public', ''))

const currentSlide = ref(heroImages.length > 0 ? 0 : -1)
let slideInterval = null

const SLIDE_DELAY = 5000

function goToSlide(i) {
  currentSlide.value = i
  resetInterval()
}

function nextSlide() {
  currentSlide.value = (currentSlide.value + 1) % heroImages.length
}

function resetInterval() {
  if (slideInterval) clearInterval(slideInterval)
  slideInterval = setInterval(nextSlide, SLIDE_DELAY)
}

// Steps data — using iconName strings for v-if rendering
const steps = [
  {
    iconName: 'Activity',
    title: 'Sense',
    desc: 'Wearable sensors continuously monitor heart rate (PPG), muscle tension (EMG), and motion patterns (IMU) — all sampled at high frequency for clinical-grade fidelity.',
    colorClass: 'step-sense',
  },
  {
    iconName: 'Brain',
    title: 'Detect',
    desc: 'A lightweight autoencoder running natively on the ESP32 learns your personal calm baseline. When physiological patterns deviate, it detects stress anomalies in real-time — no cloud needed.',
    colorClass: 'step-detect',
  },
  {
    iconName: 'Bell',
    title: 'Alert',
    desc: 'Caregivers receive instant alerts on any phone or tablet via HTTP. The companion dashboard visualizes live vitals and logs every intervention, enabling data-informed care decisions.',
    colorClass: 'step-alert',
  },
]

// Tech stack data — using iconName strings for v-if rendering
const techStack = [
  {
    iconName: 'Cpu',
    name: 'ESP32',
    desc: 'Low-power microcontroller running the entire ML pipeline at the edge',
  },
  {
    iconName: 'Brain',
    name: 'TensorFlow Lite',
    desc: 'On-device autoencoder inference for real-time anomaly detection',
  },
  {
    iconName: 'Wifi',
    name: 'HTTP',
    desc: 'Real-time Server-Sent Events push from ESP32 — no app required',
  },
  {
    iconName: 'Layers',
    name: 'Autoencoder ML',
    desc: 'Learns personal calm baselines and flags physiological deviations',
  },
  {
    iconName: 'Clock',
    name: 'FreeRTOS',
    desc: 'Deterministic real-time OS for reliable sensor sampling',
  },
  {
    iconName: 'Code2',
    name: 'Vue.js',
    desc: 'Lightweight reactive UI for the caregiver dashboard',
  },
]

// Team data — 5 real members
const team = [
  { name: 'Alvin Teo', role: 'Embedded Systems', initials: 'AT', color: 'linear-gradient(135deg, #8B5CF6, #6366F1)' },
  { name: 'Nathan Ly', role: 'Machine Learning', initials: 'NL', color: 'linear-gradient(135deg, #FDBA74, #F59E0B)' },
  { name: 'Gurnoor Bedi', role: 'Signal Processing', initials: 'GB', color: 'linear-gradient(135deg, #10B981, #6366F1)' },
  { name: 'Jennifer Wong', role: 'UX / Design', initials: 'JW', color: 'linear-gradient(135deg, #E0E7FF, #8B5CF6)' },
  { name: 'Kaiwen Ong', role: 'Clinical Research', initials: 'KO', color: 'linear-gradient(135deg, #F59E0B, #EF4444)' },
]

// ===== Scroll Reveal (IntersectionObserver) =====
// HTTP browser support check
const httpSupported = ref(true)

let observer = null

onMounted(() => {
  httpSupported.value = typeof window !== 'undefined' && 'fetch' in window
  if (heroImages.length > 1) resetInterval()

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
  if (slideInterval) clearInterval(slideInterval)
})
</script>

<style scoped>
/* ===== LANDING CONTAINER ===== */
.landing {
  overflow-x: hidden;
  overflow-y: auto;
}

/* ===== BROWSER COMPATIBILITY WARNING ===== */
.compat-warning {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.7rem 1.5rem;
  background: rgba(255, 77, 106, 0.1);
  border-bottom: 1px solid rgba(255, 77, 106, 0.2);
  color: #FF4D6A;
  font-size: 0.82rem;
  line-height: 1.4;
  text-align: center;
  justify-content: center;
  position: relative;
  z-index: 20;
}

.compat-warning strong {
  color: #FF8FA3;
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
  background: linear-gradient(180deg, #0B0914 0%, #1A1A2E 100%);
}

.hero-bg {
  position: absolute;
  inset: 0;
  overflow: hidden;
  pointer-events: none;
}

/* Slider */
.hero-slider {
  position: absolute;
  inset: 0;
}

.hero-slide {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  object-fit: cover;
  opacity: 0;
  transition: opacity 1.2s ease-in-out;
  filter: brightness(0.55) saturate(0.7);
  transform: scale(1.04);
}

.hero-slide.active {
  opacity: 1;
}

.hero-overlay {
  position: absolute;
  inset: 0;
  background: linear-gradient(
    180deg,
    rgba(11, 9, 20, 0.6) 0%,
    rgba(11, 9, 20, 0.45) 40%,
    rgba(11, 9, 20, 0.7) 100%
  );
}

/* Dots */
.hero-dots {
  position: absolute;
  bottom: 4.5rem;
  left: 50%;
  transform: translateX(-50%);
  display: flex;
  gap: 0.5rem;
  z-index: 6;
}

.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  border: 1.5px solid rgba(255, 255, 255, 0.35);
  background: transparent;
  padding: 0;
  cursor: pointer;
  transition: all 0.3s ease;
}

.dot.active {
  background: var(--accent);
  border-color: var(--accent);
  box-shadow: 0 0 8px rgba(139, 92, 246, 0.5);
  transform: scale(1.25);
}

.dot:hover:not(.active) {
  border-color: rgba(255, 255, 255, 0.6);
  background: rgba(255, 255, 255, 0.15);
}

.orb {
  position: absolute;
  border-radius: 50%;
  filter: blur(80px);
  opacity: 0.18;
}

.orb-1 {
  width: 400px;
  height: 400px;
  background: radial-gradient(circle, rgba(0, 229, 255, 0.25), transparent 70%);
  top: -100px;
  right: -100px;
  animation: drift-1 20s ease-in-out infinite;
}

.orb-2 {
  width: 300px;
  height: 300px;
  background: radial-gradient(circle, rgba(255, 0, 255, 0.2), transparent 70%);
  bottom: 10%;
  left: -80px;
  animation: drift-2 25s ease-in-out infinite;
}

.orb-3 {
  width: 250px;
  height: 250px;
  background: radial-gradient(circle, rgba(255, 122, 0, 0.2), transparent 70%);
  top: 40%;
  right: 15%;
  animation: drift-3 18s ease-in-out infinite;
}

.orb-4 {
  width: 200px;
  height: 200px;
  background: radial-gradient(circle, rgba(139, 92, 246, 0.3), transparent 70%);
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
  background: rgba(11, 9, 20, 0.5);
  backdrop-filter: blur(16px);
}

.nav-brand {
  display: flex;
  align-items: center;
  text-decoration: none;
}

.nav-logo {
  height: 32px;
  width: auto;
  object-fit: contain;
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
  background: var(--accent) !important;
  color: #fff !important;
  padding: 0.45rem 1rem;
  border-radius: var(--radius-sm);
  font-weight: 600 !important;
  transition: background var(--transition-fast) !important;
}

.nav-cta:hover {
  background: var(--accent-hover) !important;
  color: #fff !important;
}

/* Hero content */
.hero-content {
  max-width: 680px;
  z-index: 8;
}

.hero-badge {
  display: inline-block;
  padding: 0.35rem 1rem;
  border-radius: 100px;
  border: 1px solid var(--accent);
  background: rgba(139, 92, 246, 0.1);
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
  background: linear-gradient(135deg, #00E5FF 0%, #FF00FF 50%, #8B5CF6 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.hero-subtitle {
  font-size: clamp(1.05rem, 2.5vw, 1.35rem);
  font-weight: 500;
  color: var(--accent);
  margin-bottom: 1rem;
}

.hero-tagline {
  font-size: 1rem;
  line-height: 1.7;
  color: var(--text-secondary);
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
  color: #fff;
}

.btn-primary:hover {
  background: var(--accent-hover);
  transform: translateY(-2px);
  box-shadow: 0 8px 24px rgba(139, 92, 246, 0.3);
}

.btn-outline {
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid var(--border-hover);
  color: var(--text-secondary);
}

.btn-outline:hover {
  border-color: var(--accent);
  color: var(--text-primary);
  background: rgba(255, 255, 255, 0.08);
  transform: translateY(-2px);
  box-shadow: 0 4px 20px rgba(139, 92, 246, 0.3);
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
  color: var(--text-primary);
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

.about-visual {
  width: 100%;
  aspect-ratio: 4 / 3;
  border-radius: var(--radius-lg);
  background: linear-gradient(135deg, rgba(139, 92, 246, 0.08), rgba(0, 229, 255, 0.06));
  border: 1px solid rgba(139, 92, 246, 0.15);
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 1rem;
  position: relative;
  overflow: hidden;
}

.about-visual-icon {
  color: var(--accent);
  position: relative;
  z-index: 1;
}

.about-visual-label {
  color: var(--text-secondary);
  font-size: 0.85rem;
  font-weight: 500;
  position: relative;
  z-index: 1;
}

.about-visual-pulse {
  position: absolute;
  width: 120px;
  height: 120px;
  border-radius: 50%;
  background: rgba(139, 92, 246, 0.12);
  animation: pulse-ring 3s ease-in-out infinite;
}

.about-visual-pulse-2 {
  width: 180px;
  height: 180px;
  background: rgba(0, 229, 255, 0.06);
  animation-delay: 1.5s;
}

@keyframes pulse-ring {
  0%, 100% { transform: scale(0.8); opacity: 0.5; }
  50% { transform: scale(1.2); opacity: 1; }
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
.step-alert::before { background: var(--accent-orange); }

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
  background: rgba(255, 77, 106, 0.12);
  color: var(--color-danger);
}

.step-detect .step-icon-wrap {
  background: rgba(0, 229, 255, 0.1);
  color: var(--color-info);
}

.step-alert .step-icon-wrap {
  background: rgba(255, 122, 0, 0.12);
  color: var(--accent-orange);
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
  color: var(--text-primary);
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
  box-shadow: var(--shadow-md);
}

.tech-icon {
  color: var(--accent);
  margin-bottom: 0.75rem;
}

.tech-name {
  font-size: 0.95rem;
  font-weight: 600;
  margin-bottom: 0.35rem;
  color: var(--text-primary);
}

.tech-desc {
  font-size: 0.8rem;
  color: var(--text-muted);
  line-height: 1.5;
}

/* ===== TEAM ===== */
.team-section {
  background: linear-gradient(180deg, var(--bg-primary) 0%, var(--bg-secondary) 100%);
}

.team-grid {
  display: grid;
  grid-template-columns: repeat(5, 1fr);
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
  color: #fff;
  letter-spacing: 1px;
}

.team-name {
  font-size: 0.95rem;
  font-weight: 600;
  margin-bottom: 0.25rem;
  color: var(--text-primary);
}

.team-role {
  font-size: 0.8rem;
  color: var(--text-muted);
}

/* ===== FOOTER ===== */
.landing-footer {
  border-top: 1px solid var(--border);
  padding: 3rem 1.5rem;
  background: var(--card-bg);
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

.team-grid .reveal:nth-child(5) {
  transition-delay: 0.4s;
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
    grid-template-columns: repeat(3, 1fr);
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
