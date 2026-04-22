import { createRouter, createWebHistory } from 'vue-router'

const routes = [
  { path: '/', name: 'Landing', component: () => import('./views/LandingPage.vue') },
  { path: '/dashboard', name: 'Dashboard', component: () => import('./views/DashboardPage.vue') },
  { path: '/docs', name: 'Docs', component: () => import('./views/DocsPage.vue') },
  { path: '/:pathMatch(.*)*', redirect: '/' },
]

export default createRouter({
  history: createWebHistory(),
  routes,
})
