<template>
  <div class="detail">
    <p class="back">
      <router-link to="/">‹ 返回总览</router-link>
    </p>

    <section class="card" v-if="info.prescription">
      <h2 class="title ai-title">💊 工业4.0 电子处方与柔性决策</h2>
      
      <div v-if="info.prescription.warnings?.length" class="warnings">
        <div v-for="w in info.prescription.warnings" :key="w" class="warning-box">
          ⚠️ {{ w }}
        </div>
      </div>

      <div class="timeline" v-if="info.prescription.timeline?.length">
        <div v-for="(t, idx) in info.prescription.timeline" :key="idx" :class="['timeline-item', t.status]">
          <div class="dot"></div>
          <div class="content">
            <span class="date">{{ t.date }}</span>
            <span class="event">{{ t.event }}</span>
          </div>
        </div>
      </div>

      <div class="recommendations">
        <div v-for="r in info.prescription.recommendations" :key="r.pesticide_id" 
             :class="['rec-card', { locked: r.is_locked, warn: !r.is_recommended && !r.is_locked }]">
          <div class="rec-header">
            <h4>{{ r.name }} ({{ r.type }})</h4>
            <span class="tag" v-for="tag in r.tags" :key="tag">{{ tag }}</span>
          </div>
          <div class="rec-body">
            <p>剂量基准: {{ r.base_dosage }} | PHI安全期: {{ r.phi_days }}天</p>
            <p class="reason">{{ r.reason }}</p>
          </div>
        </div>
      </div>
    </section>

    <section class="card">
      <h2 class="title">地块信息</h2>
      <p class="line">编码：{{ info.plot?.code ?? "—" }}</p>
      <p class="line">名称：{{ info.plot?.name ?? "—" }}</p>
      <p class="line">面积：{{ info.plot?.area_m2 ?? 0 }} m²</p>
      <p class="line">作物：{{ info.current_crop_cycle?.variety ?? "—" }}</p>
      <p class="line">种植日期：{{ info.current_crop_cycle?.planted_at ?? "—" }}</p>
    </section>

    <section class="card">
      <h2 class="title">病虫害历史</h2>
      <p v-if="!info.pest_history?.length" class="muted">暂无记录</p>
      <div v-for="(item, idx) in info.pest_history" :key="idx" class="row">
        <div class="row-main">{{ item.detected_at }}</div>
        <div class="row-sub">{{ item.pest_type }} / 严重度 {{ item.severity }}</div>
      </div>
    </section>

    <section class="card">
      <h2 class="title">作业历史</h2>
      <p v-if="!info.operation_history?.length" class="muted">暂无记录</p>
      <div v-for="(item, idx) in info.operation_history" :key="idx" class="row">
        <div class="row-main">{{ item.created_at }}</div>
        <div class="row-sub">
          {{ item.action_type }} {{ item.amount ?? "" }}{{ item.unit ?? "" }}
        </div>
        <div class="row-sub">{{ item.result }}</div>
      </div>
    </section>

    <div class="actions">
      <button type="button" class="btn" :disabled="loading || !code" @click="loadData">
        {{ loading ? "加载中…" : "刷新" }}
      </button>
    </div>

    <p v-if="error" class="error">{{ error }}</p>
  </div>
</template>

<script setup>
import { onMounted, reactive, ref, watch } from "vue";
import { getPlotHistory, getFarmPlotPrescription } from "../api/client.js";

const props = defineProps({
  code: { type: String, required: true },
});

const info = reactive({
  plot: {},
  current_crop_cycle: null,
  pest_history: [],
  operation_history: [],
  prescription: null,
});

const loading = ref(false);
const error = ref("");

async function loadData() {
  if (!props.code || loading.value) return;
  loading.value = true;
  error.value = "";
  try {
    const data = await getPlotHistory(props.code);
    info.plot = data.plot ?? {};
    info.current_crop_cycle = data.current_crop_cycle ?? null;
    info.pest_history = data.pest_history ?? [];
    info.operation_history = data.operation_history ?? [];

    const rxData = await getFarmPlotPrescription(props.code);
    if(rxData) {
      info.prescription = rxData;
    }
  } catch (e) {
    error.value = "地块详情拉取失败，请检查编码或后端是否运行。";
    console.error(e);
  } finally {
    loading.value = false;
  }
}

onMounted(loadData);
watch(
  () => props.code,
  () => loadData(),
);
</script>

<style scoped>
.detail {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.back {
  margin: 0 0 4px;
}

.back a {
  color: var(--accent);
  text-decoration: none;
  font-weight: 600;
}

.back a:hover {
  text-decoration: underline;
}

.card {
  background: var(--card);
  border-radius: 12px;
  padding: 18px 20px;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.06);
  border: 1px solid var(--border);
}

.title {
  margin: 0 0 12px;
  font-size: 1.1rem;
  font-weight: 700;
  color: var(--accent);
}

.line {
  margin: 8px 0 0;
}

.muted {
  color: var(--muted);
  margin: 0;
}

.row {
  border-top: 1px solid var(--border);
  padding: 12px 0;
}

.row:first-of-type {
  border-top: none;
  padding-top: 4px;
}

.row-main {
  font-weight: 600;
}

.row-sub {
  margin-top: 4px;
  color: var(--muted);
  font-size: 0.92rem;
}

.actions {
  margin-top: 4px;
}

.btn {
  width: 100%;
  padding: 12px 16px;
  border: none;
  border-radius: 10px;
  background: var(--accent);
  color: #fff;
  font-weight: 600;
}

.btn:disabled {
  opacity: 0.65;
  cursor: not-allowed;
}

.error {
  color: #b71c1c;
  font-size: 0.9rem;
  margin: 0;
}

.ai-title {
  color: #2e7d32;
  border-bottom: 2px solid rgba(46, 125, 50, 0.2);
  padding-bottom: 8px;
}

.warnings {
  margin-bottom: 16px;
}

.warning-box {
  background: #fff3e0;
  border-left: 4px solid #ff9800;
  padding: 10px 14px;
  font-size: 0.95rem;
  border-radius: 4px;
  color: #e65100;
  margin-bottom: 8px;
  font-weight: 600;
}

.timeline {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin: 16px 0;
  padding-left: 8px;
  border-left: 2px solid #e0e0e0;
}

.timeline-item {
  position: relative;
  padding-left: 16px;
}

.timeline-item .dot {
  position: absolute;
  left: -21px;
  top: 4px;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: #bdbdbd;
  border: 2px solid #fff;
}

.timeline-item.past .dot {
  background: #9e9e9e;
}
.timeline-item.current .dot {
  background: #2196f3;
  box-shadow: 0 0 0 2px rgba(33, 150, 243, 0.3);
}
.timeline-item.future .dot {
  background: #ffb300;
}

.timeline-item .content {
  display: flex;
  justify-content: space-between;
  font-size: 0.9rem;
}
.timeline-item .date {
  color: var(--muted);
  font-family: monospace;
}
.timeline-item .event {
  font-weight: 500;
}

.recommendations {
  display: grid;
  gap: 12px;
}

.rec-card {
  background: #f1f8e9;
  border: 1px solid #c5e1a5;
  border-radius: 8px;
  padding: 14px;
}

.rec-card.locked {
  background: #ffebee;
  border-color: #ffcdd2;
  opacity: 0.85;
}

.rec-card.warn {
  background: #fff8e1;
  border-color: #ffecb3;
}

.rec-header {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}

.rec-header h4 {
  margin: 0;
  font-size: 1rem;
  color: #333;
}

.tag {
  font-size: 0.75rem;
  padding: 2px 6px;
  border-radius: 4px;
  background: #fff;
  border: 1px solid #ddd;
}

.rec-card:not(.locked):not(.warn) .tag {
  background: #2e7d32;
  color: white;
  border-color: #2e7d32;
}

.rec-card.locked .tag {
  background: #c62828;
  color: white;
  border-color: #c62828;
}

.rec-card.warn .tag {
  background: #f57f17;
  color: white;
  border-color: #f57f17;
}

.rec-body {
  font-size: 0.9rem;
  color: #555;
}

.rec-body p {
  margin: 4px 0;
}

.rec-body .reason {
  margin-top: 8px;
  font-weight: 600;
  color: #424242;
}
</style>
