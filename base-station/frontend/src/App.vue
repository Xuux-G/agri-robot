<script setup>
import { computed } from "vue";
import { useRouter, useRoute } from "vue-router";

const router = useRouter();
const route = useRoute();

const menuItems = [
  { path: "/", label: "指挥大屏", desc: "Dashboard" },
  { path: "/monitor", label: "作业监控", desc: "Live Monitor" },
  { path: "/archives", label: "电子药历", desc: "Plot Archives" },
  { path: "/knowledge", label: "植保知识库", desc: "Knowledge Base" },
];

const activePath = computed(() => route.path);

function navigate(path) {
  router.push(path);
}
</script>

<template>
  <div class="layout">
    <!-- 侧边栏 - 仿 agri.cn 的竖向导航风格 -->
    <aside class="sidebar">
      <div class="brand-block">
        <div class="brand-icon">
          <svg width="36" height="36" viewBox="0 0 48 48" fill="none">
            <rect width="48" height="48" rx="12" fill="#3B6B3B"/>
            <path d="M14 36c0-6 4-14 10-18s10-2 10 4-4 10-10 14-10 6-10 0z" fill="#7CB342"/>
            <path d="M24 18c4-2 8 0 8 4s-2 8-8 10" stroke="#E8F5E9" stroke-width="2" fill="none"/>
          </svg>
        </div>
        <div class="brand-text">
          <h1>Agri Guardian</h1>
          <p>智能植保指挥系统</p>
        </div>
      </div>

      <nav class="menu">
        <button
          v-for="item in menuItems"
          :key="item.path"
          :class="['menu-item', { active: activePath === item.path }]"
          @click="navigate(item.path)"
        >
          <span class="menu-label">{{ item.label }}</span>
          <span class="menu-desc">{{ item.desc }}</span>
        </button>
      </nav>

      <div class="sidebar-footer">
        <div class="status-dot"></div>
        <span>系统运行中</span>
      </div>
    </aside>

    <!-- 主内容区 - 纯路由出口 -->
    <main class="main">
      <router-view v-slot="{ Component }">
        <transition name="page-fade" mode="out-in">
          <component :is="Component" />
        </transition>
      </router-view>
    </main>
  </div>
</template>

<style scoped>
/* ===== 设计系统 =====
   参考 agri.cn / amic.agri.cn / bh.ainyi.cn
   原则：不平涂纯色背景、不用 emoji 图标、不对齐居中、不空洞
   ==================== */

.layout {
  height: 100vh;
  display: grid;
  grid-template-columns: 260px 1fr;
  background:
    /* 噪点纹理叠加 */
    url("data:image/svg+xml,%3Csvg viewBox='0 0 200 200' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.75' numOctaves='4' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)' opacity='0.035'/%3E%3C/svg%3E"),
    /* 底部渐变 - 从土壤色到浅绿 */
    linear-gradient(175deg, #f5f0e8 0%, #edf5e9 35%, #e8f0e3 70%, #f0ece0 100%);
}

.sidebar {
  display: flex;
  flex-direction: column;
  padding: 28px 20px 24px;
  background:
    url("data:image/svg+xml,%3Csvg viewBox='0 0 200 200' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.8' numOctaves='3' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)' opacity='0.04'/%3E%3C/svg%3E"),
    linear-gradient(180deg, rgba(255,255,255,0.92) 0%, rgba(240,245,235,0.95) 100%);
  border-right: 1px solid rgba(59, 107, 59, 0.1);
  box-shadow: 2px 0 24px rgba(30, 60, 30, 0.04);
  gap: 32px;
}

.brand-block {
  display: flex;
  align-items: center;
  gap: 14px;
}

.brand-icon {
  flex-shrink: 0;
}

.brand-text h1 {
  margin: 0;
  font-size: 1.15rem;
  font-weight: 700;
  color: #2d5a27;
  letter-spacing: 0.5px;
}

.brand-text p {
  margin: 4px 0 0;
  font-size: 0.75rem;
  color: #6b8a65;
  letter-spacing: 1px;
}

.menu {
  display: flex;
  flex-direction: column;
  gap: 6px;
  flex: 1;
}

.menu-item {
  display: flex;
  flex-direction: column;
  gap: 2px;
  text-align: left;
  border: none;
  background: transparent;
  padding: 14px 16px;
  border-radius: 10px;
  cursor: pointer;
  transition: all 0.25s ease;
  position: relative;
  color: #4a6740;
}

.menu-item:hover {
  background: rgba(59, 107, 59, 0.06);
}

.menu-item.active {
  background: rgba(59, 107, 59, 0.1);
  border-left: 3px solid #3B6B3B;
}

.menu-label {
  font-size: 0.95rem;
  font-weight: 600;
  color: #2d4a22;
}

.menu-desc {
  font-size: 0.7rem;
  color: #8a9e7f;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.sidebar-footer {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 12px 16px;
  font-size: 0.8rem;
  color: #6b8a65;
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #4caf50;
  animation: pulse-dot 2s infinite;
}

@keyframes pulse-dot {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.4; }
}

.main {
  min-width: 0;
  overflow: hidden;
  padding: 16px;
}

/* 页面过渡动效 */
.page-fade-enter-active,
.page-fade-leave-active {
  transition: opacity 0.2s ease, transform 0.2s ease;
}

.page-fade-leave-active {
  transition: opacity 0.15s ease, transform 0.15s ease;
}

.page-fade-enter-from {
  opacity: 0;
  transform: translateY(6px);
}

.page-fade-leave-to {
  opacity: 0;
  transform: translateY(-4px);
}
</style>
