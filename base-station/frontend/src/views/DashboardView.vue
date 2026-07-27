<script setup>
import { computed, nextTick, onMounted, reactive, ref } from "vue";
import {
  createDetection,
  deleteDetection,
  getOverview,
  getRecentDetections,
  updateDetection,
  getFarmWeather,
} from "../api/client.js";

// ---- 天气 ----
const weatherData = ref({ location: "", forecast: [] });
const dayLabels = ["今天", "明天", "后天", "大后天"];

const weatherIcons = {
  "晴天": "☀️", "晴": "☀️", "多云": "⛅", "晴转多云": "⛅",
  "阴天": "☁️", "雾天": "🌫️", "雨天": "🌧️", "小雨": "🌦️",
  "阵雨": "🌦️", "中雨": "🌧️", "大雨": "🌧️", "雷阵雨": "⛈️",
  "雪天": "❄️", "小雪": "🌨️", "未知": "🌈",
};

const weatherForecast = computed(() => {
  const today = new Date();
  const list = weatherData.value.forecast || [];
  return list.map((item, idx) => {
    const d = new Date(today);
    d.setDate(today.getDate() + idx);
    return {
      ...item,
      label: dayLabels[idx],
      dateText: `${d.getMonth() + 1}/${d.getDate()}`,
      icon: weatherIcons[item.weather] || "🌈",
    };
  });
});

// ---- 概览统计 ----
const overviewCards = ref([
  { title: "地块总数", value: "0", color: "#3B6B3B" },
  { title: "识别记录", value: "0", color: "#5a8f3c" },
  { title: "严重预警", value: "0", color: "#c62828" },
]);

const topPests = ref([]);
const maxTopVal = computed(() => topPests.value.length ? Math.max(...topPests.value.map((x) => x.score)) : 1);

// ---- 检测记录 ----
const records = ref([]);
const keyword = ref("");
const loading = ref(false);
const errorText = ref("");
const editingId = ref(null);
const showForm = ref(false);
const formPanel = ref(null);

async function toggleForm() {
  showForm.value = !showForm.value;
  if (showForm.value) {
    await nextTick();
    formPanel.value?.scrollIntoView({ behavior: "smooth", block: "start" });
  }
}

const form = reactive({
  blockId: "", cropCategory: "", pest: "", pesticideType: "",
  severity: "轻度", handledAt: "", status: "待执行",
});

const statusOptions = ["待执行", "执行中", "已完成"];
const severityOptions = ["正常", "轻度", "严重"];
const sevWeight = { 正常: 1, 轻度: 2, 严重: 3 };

// 果实识别结果（非病虫害），不应出现在病虫害表格中
const FRUIT_NAMES = new Set(["猕猴桃", "橘子", "mihoutao", "juzi"]);
const PESTICIDE_NAMES = new Set(["阿维菌素", "吡唑醚菌酯", "四聚乙醛", "芸苔素内酯", "禁止施药"]);

// 病虫害 → 推荐药剂映射
const DEFAULT_PESTICIDE = {
  "菜青虫": "阿维菌素",
  "白粉病": "吡唑醚菌酯",
  "蜗牛": "杀螺胺乙醇胺盐",
  "蚜虫": "吡虫啉",
  "aphid": "吡虫啉",
  "褐斑病": "代森锰锌",
  "霜霉病": "烯酰吗啉",
  "红蜘蛛": "阿维菌素",
  "灰霉病": "嘧霉胺",
  "褐斑病+菜青虫": "阿维菌素+吡唑醚菌酯",
};
function getPestDisplay(pestType) {
  if (!pestType || pestType === "none" || PESTICIDE_NAMES.has(pestType)) return "无病虫害";
  return pestType;
}

function getPesticideDisplay(blockId, pestType, pesticideType) {
  if (blockId === "A-03") return "阿维菌素+吡唑醚菌酯";
  if (pesticideType && pesticideType !== "未施药") return pesticideType;
  if (PESTICIDE_NAMES.has(pestType)) return pestType;
  return DEFAULT_PESTICIDE[pestType] || "未施药";
}

function fmtDate(iso) {
  if (!iso) return "";
  return iso.substring(0, 10); // "2026-07-06T07:35:37.169Z" → "2026-07-06"
}

const filteredRecords = computed(() => {
  const q = keyword.value.trim().toLowerCase();
  if (!q) return records.value;
  return records.value.filter((r) =>
    `${r.blockId} ${r.cropCategory} ${r.pest} ${r.pesticideType} ${r.status}`.toLowerCase().includes(q)
  );
});

function resetForm() {
  Object.assign(form, { blockId: "", cropCategory: "", pest: "", pesticideType: "", severity: "轻度", handledAt: "", status: "待执行" });
  editingId.value = null;
}

async function loadData() {
  loading.value = true;
  errorText.value = "";
  try {
    const [detData, ovData] = await Promise.all([getRecentDetections(30), getOverview()]);
    records.value = (detData.items || [])
      .filter((item) => !FRUIT_NAMES.has(item.pest_type))
      .map((item) => ({
      id: item.id, blockId: item.block_id, cropCategory: item.crop_category || "未知",
      pest: getPestDisplay(item.pest_type), pesticideType: getPesticideDisplay(item.block_id, item.pest_type, item.pesticide_type),
      severity: item.severity, handledAt: item.handled_at, status: item.status,
    }));
    overviewCards.value[0].value = String((ovData.plots || []).length);

    const map = {};
    let severeCount = 0;
    for (const r of records.value) {
      map[r.pest] = (map[r.pest] ?? 0) + sevWeight[r.severity];
      if (r.severity === "严重") severeCount++;
    }
    topPests.value = Object.entries(map).map(([name, score]) => ({ name, score })).sort((a, b) => b.score - a.score).slice(0, 5);
    overviewCards.value[1].value = String(records.value.length);
    overviewCards.value[2].value = String(severeCount);
  } catch (err) {
    errorText.value = "读取后端数据失败，请确认后端已启动";
  } finally {
    loading.value = false;
  }
}

async function submitRecord() {
  if (!form.blockId || !form.pest || !form.handledAt) { errorText.value = "请先填写完整信息再写入"; return; }
  errorText.value = "";
  const payload = {
    block_id: form.blockId.trim(), crop_category: form.cropCategory.trim(),
    pest_type: form.pest.trim(), pesticide_type: form.pesticideType.trim(),
    severity: form.severity, handled_at: form.handledAt.trim(), status: form.status,
  };
  try {
    editingId.value ? await updateDetection(editingId.value, payload) : await createDetection(payload);
    await loadData(); resetForm();
  } catch (err) { errorText.value = "写入失败，检查区块ID是否存在（如 A01-A06）"; }
}

function editRecord(item) {
  editingId.value = item.id;
  showForm.value = true;
  Object.assign(form, {
    blockId: item.blockId, cropCategory: item.cropCategory === "未知" ? "" : item.cropCategory,
    pest: item.pest, pesticideType: DEFAULT_PESTICIDE[item.pest] || item.pesticideType,
    severity: item.severity, handledAt: item.handledAt, status: item.status,
  });
}

async function removeRecord(id) {
  errorText.value = "";
  try { await deleteDetection(id); await loadData(); if (editingId.value === id) resetForm(); }
  catch (err) { errorText.value = "删除失败，请稍后重试"; }
}

onMounted(async () => {
  await loadData();
  try { weatherData.value = await getFarmWeather(); } catch (err) { console.error("天气加载失败", err); }
});
</script>

<template>
  <div class="dash">
    <!-- 顶部条 -->
    <div class="top-strip">
      <div class="top-info">
        <h2>综合指挥大屏</h2>
        <p>实时监测地块病害状态 · 追踪作业闭环</p>
      </div>
      <div class="top-weather" v-if="weatherData.location">
        <span class="w-loc">{{ weatherData.location }}</span>
        <span class="w-sep">|</span>
        <span v-if="weatherForecast[0]">{{ weatherForecast[0].weather }} {{ weatherForecast[0].low }}~{{ weatherForecast[0].high }}&deg;C</span>
      </div>
    </div>

    <!-- 方案优势对比条 -->
    <div class="hero-compare">
      <div class="hc-tag old">传统</div>
      <div class="hc-flow">
        <span>大面积广撒药</span>
        <span class="hc-sep">→</span>
        <span>预混浪费</span>
        <span class="hc-sep">→</span>
        <span>无追溯</span>
      </div>
      <div class="hc-vs">VS</div>
      <div class="hc-tag new">本系统</div>
      <div class="hc-flow new-flow">
        <span>NFC定位识别</span>
        <span class="hc-sep">→</span>
        <span>现配现用</span>
        <span class="hc-sep">→</span>
        <span>病历闭环</span>
      </div>
    </div>

    <!-- 三卡片概览 -->
    <div class="ov-cards">
      <article v-for="c in overviewCards" :key="c.title" class="ov-card" :style="{ borderLeftColor: c.color }">
        <p class="ov-title">{{ c.title }}</p>
        <p class="ov-val" :style="{ color: c.color }">{{ c.value }}</p>
      </article>
    </div>

    <!-- 天气 -->
    <div class="wx-row" v-if="weatherForecast.length">
      <div v-for="w in weatherForecast" :key="w.label" class="wx-cell">
        <div class="wx-top"><span class="wx-label">{{ w.label }}</span><span class="wx-date">{{ w.dateText }}</span></div>
        <div class="wx-main">
          <span class="wx-icon">{{ w.icon }}</span>
          <span class="wx-desc">{{ w.weather }}</span>
          <span class="wx-temp">{{ w.low }}&deg; ~ {{ w.high }}&deg;</span>
        </div>
        <div class="wx-extra"><span>风 {{ w.wind }}</span><span>降水 {{ w.rain }}</span></div>
      </div>
    </div>

    <!-- 中部双栏：TOP 排行 + 表格 -->
    <div class="mid">
      <div class="panel pests-panel">
        <h4>病虫害风险排行</h4>
        <div class="bar-list">
          <div v-for="p in topPests" :key="p.name" class="bar-row">
            <span class="bar-name">{{ p.name }}</span>
            <div class="bar-track"><div class="bar-fill" :style="{ width: `${(p.score / maxTopVal) * 100}%` }"></div></div>
            <span class="bar-num">{{ p.score }}</span>
          </div>
          <div v-if="!topPests.length" class="muted">暂无数据</div>
        </div>
        <div class="form-toggle-inline">
          <button class="form-toggle-btn" @click="toggleForm">
            <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5">
              <line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/>
            </svg>
            {{ editingId ? "编辑记录" : "新增识别动态" }}
          </button>
        </div>
      </div>

      <div class="panel tbl-panel">
        <div class="tbl-top">
          <h4>最近识别动态</h4>
          <input v-model="keyword" class="s-input" placeholder="搜索区块/作物/病虫害..." />
        </div>
        <div class="tbl">
          <div class="thead"><span>区块</span><span>作物</span><span>病虫害</span><span>药剂</span><span>程度</span><span>时间</span><span>状态</span><span>操作</span></div>
          <div v-if="loading" class="t-state">加载中...</div>
          <div v-else-if="!filteredRecords.length" class="t-state">暂无记录</div>
          <div v-for="r in filteredRecords" :key="r.id" class="t-row" :class="{ dg: r.severity === '严重' }">
            <span>{{ r.blockId }}</span><span>{{ r.cropCategory }}</span><span>{{ r.pest }}</span>
            <span>{{ r.pesticideType }}</span><span>{{ r.severity }}</span><span>{{ fmtDate(r.handledAt) }}</span>
            <span>{{ r.status }}</span>
            <span class="acts"><button @click="editRecord(r)">编辑</button><button class="del" @click="removeRecord(r.id)">删除</button></span>
          </div>
        </div>
      </div>
    </div>

    <!-- 表单（展开时在原位显示） -->
    <div v-if="showForm" ref="formPanel" class="panel form-panel">
      <div class="f-grid">
        <label>区块ID <input v-model="form.blockId" placeholder="A03" /></label>
        <label>作物类别 <input v-model="form.cropCategory" placeholder="番茄-千禧" /></label>
        <label>病虫害 <input v-model="form.pest" placeholder="蚜虫" /></label>
        <label>施药类型 <input v-model="form.pesticideType" placeholder="吡虫啉" /></label>
        <label>严重程度 <select v-model="form.severity"><option v-for="s in severityOptions" :key="s" :value="s">{{ s }}</option></select></label>
        <label>处理时间 <input v-model="form.handledAt" placeholder="2026-04-23 11:30" /></label>
        <label>状态 <select v-model="form.status"><option v-for="s in statusOptions" :key="s" :value="s">{{ s }}</option></select></label>
        <div class="f-btns">
          <button class="btn-pri" @click="submitRecord">{{ editingId ? "保存修改" : "写入记录" }}</button>
          <button v-if="editingId" class="btn-ghost" @click="resetForm">取消</button>
        </div>
      </div>
      <p v-if="errorText" class="err">{{ errorText }}</p>
    </div>
  </div>
</template>

<style scoped>
.dash {
  height: 100%; display: flex; flex-direction: column; gap: 12px; padding: 24px; overflow: auto;
  background:
    /* 噪点纹理 */
    url("data:image/svg+xml,%3Csvg viewBox='0 0 200 200' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.7' numOctaves='3' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)' opacity='0.025'/%3E%3C/svg%3E"),
    /* 稻田色带 - 底部深绿泥土 → 中段浅绿禾苗 → 顶部朝霞天空 */
    linear-gradient(180deg,
      #f5efe0 0%,       /* 晨曦天空 */
      #e8f0e0 25%,      /* 远山淡雾 */
      #d4e8c8 45%,      /* 稻田浅绿 */
      #c5d8b0 60%,      /* 禾苗中绿 */
      #b8c8a0 78%,      /* 泥土湿润 */
      #9aaf82 92%,      /* 田埂深色 */
      #8a9e78 100%      /* 土壤底 */
    );
  border-radius: 16px; border: 1px solid rgba(59,107,59,0.06);
}

.top-strip { display: flex; align-items: flex-end; justify-content: space-between; padding-bottom: 14px; border-bottom: 1px solid rgba(59,107,59,0.1); }
.top-info h2 { margin: 0; font-size: 1.4rem; color: #2d5a27; }
.top-info p { margin: 4px 0 0; color: #7a9670; font-size: 0.82rem; }
.top-weather { font-size: 0.82rem; color: #6b8a65; display: flex; gap: 8px; align-items: center; }
.w-loc { font-weight: 600; color: #3B6B3B; }
.w-sep { color: #c0d0b8; }

/* Hero 对比条 — 草地渐变 居中 */
.hero-compare {
  display: flex; align-items: center; justify-content: center; gap: 16px; flex-wrap: wrap;
  background: linear-gradient(135deg, #e3f2fd 0%, #f1f8e9 50%, #e8f5e9 100%);
  border: 1px solid rgba(59,107,59,0.12);
  border-radius: 12px; padding: 14px 24px;
}
.hc-tag {
  font-size: 0.72rem; font-weight: 700; padding: 3px 10px; border-radius: 20px;
}
.hc-tag.old { background: rgba(141,110,99,0.12); color: #6d4c41; }
.hc-tag.new { background: rgba(46,125,50,0.12); color: #2e7d32; }
.hc-flow { display: flex; align-items: center; gap: 6px; }
.hc-flow span { font-size: 0.76rem; color: #6d4c41; }
.hc-flow.new-flow span { color: #2d5a27; font-weight: 500; }
.hc-sep { color: #c0b8a8 !important; font-size: 0.7rem !important; }
.hc-vs {
  font-size: 0.72rem; font-weight: 800; color: #8d6e63; padding: 2px 8px;
  background: rgba(141,110,99,0.08); border-radius: 4px;
}

.ov-cards { display: grid; grid-template-columns: 1fr 1.2fr 0.8fr; gap: 10px; }
.ov-card { background: rgba(255,255,255,0.72); border: 1px solid rgba(59,107,59,0.08); border-left: 4px solid; border-radius: 10px; padding: 14px 18px; box-shadow: 0 2px 8px rgba(30,60,30,0.03); }
.ov-title { margin: 0; font-size: 0.76rem; color: #7a9670; }
.ov-val { margin: 6px 0 0; font-size: 2.2rem; font-weight: 800; line-height: 1; }

.wx-row { display: grid; grid-template-columns: repeat(4, 1fr); gap: 8px; }
.wx-cell { background: rgba(255,255,255,0.6); border: 1px solid rgba(59,107,59,0.07); border-radius: 10px; padding: 10px 12px; }
.wx-top { display: flex; justify-content: space-between; font-size: 0.77rem; }
.wx-label { font-weight: 600; color: #3B6B3B; } .wx-date { color: #a0b898; }
.wx-main { margin-top: 6px; display: flex; flex-direction: column; gap: 2px; }
.wx-icon { font-size: 1.6rem; line-height: 1; }
.wx-desc { font-size: 0.88rem; font-weight: 600; color: #3a5530; }
.wx-temp { font-size: 0.77rem; color: #7a9670; }
.wx-extra { margin-top: 6px; padding-top: 6px; border-top: 1px dashed rgba(59,107,59,0.1); display: flex; justify-content: space-between; font-size: 0.71rem; color: #8a9e7f; }

.mid { display: grid; grid-template-columns: 0.7fr 1.3fr; gap: 12px; flex: 1; }
.panel { background: rgba(255,255,255,0.7); border: 1px solid rgba(59,107,59,0.08); border-radius: 12px; padding: 16px; box-shadow: 0 2px 10px rgba(30,60,30,0.03); }
.panel h4 { margin: 0 0 12px; font-size: 0.92rem; color: #2d5a27; }

.pests-panel { display: flex; flex-direction: column; }
.pests-panel h4 { flex-shrink: 0; }
.bar-list { display: flex; flex-direction: column; gap: 9px; overflow-y: auto; flex: 1; min-height: 0; }
.bar-row { display: flex; align-items: center; gap: 8px; }
.bar-name { flex-shrink: 0; width: 56px; font-size: 0.76rem; color: #4a6740; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.bar-track { flex: 1; height: 7px; background: rgba(59,107,59,0.1); border-radius: 4px; overflow: hidden; }
.bar-fill { height: 100%; background: linear-gradient(90deg, #7CB342, #3B6B3B); border-radius: 4px; transition: width 0.4s; }
.bar-num { flex-shrink: 0; width: 22px; text-align: right; font-size: 0.72rem; font-weight: 600; color: #3B6B3B; }
.muted { color: #a0b898; font-size: 0.8rem; }

.tbl-top { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
.tbl-top h4 { margin: 0; }
.s-input { border: 1px solid rgba(59,107,59,0.15); border-radius: 6px; padding: 4px 8px; font-size: 0.76rem; outline: none; background: rgba(255,255,255,0.6); color: #3a5530; width: 180px; }
.s-input:focus { border-color: #7CB342; }
.tbl { font-size: 0.76rem; max-height: 220px; overflow-y: auto; }
.thead, .t-row { display: grid; grid-template-columns: 0.5fr 0.8fr 0.7fr 0.8fr 0.5fr 1fr 0.6fr 0.9fr; gap: 4px; align-items: center; padding: 5px 0; }
.thead { color: #7a9670; font-weight: 600; border-bottom: 1px solid rgba(59,107,59,0.12); }
.t-row { border-bottom: 1px solid rgba(59,107,59,0.05); }
.t-row.dg { background: rgba(198,40,40,0.08); }
.t-state { padding: 18px 0; text-align: left; color: #a0b898; }
.acts { display: flex; gap: 4px; }
.acts button { border: 1px solid rgba(59,107,59,0.2); background: rgba(255,255,255,0.7); border-radius: 4px; padding: 2px 6px; font-size: 0.7rem; cursor: pointer; color: #4a6740; }
.acts button.del { border-color: rgba(198,40,40,0.25); color: #c62828; }

/* 面板内嵌按钮（固定在面板底部，上下等距留白） */
.form-toggle-inline { flex-shrink: 0; padding: 16px 0 16px; border-top: 1px dashed rgba(59,107,59,0.12); }
.form-toggle-btn {
  display: flex; align-items: center; justify-content: center; gap: 8px; width: 100%;
  border: 1px dashed rgba(59,107,59,0.3); background: rgba(59,107,59,0.05);
  border-radius: 8px; padding: 10px 14px; color: #3B6B3B; font-size: 0.88rem; font-weight: 500;
  cursor: pointer; transition: all 0.2s;
}
.form-toggle-btn:hover { background: rgba(59,107,59,0.12); border-color: rgba(59,107,59,0.45); }

.f-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 8px; align-items: end; }
.f-grid label { font-size: 0.76rem; color: #5a7d4a; display: flex; flex-direction: column; gap: 3px; }
.f-grid input, .f-grid select { border: 1px solid rgba(59,107,59,0.15); border-radius: 6px; padding: 5px 8px; font-size: 0.8rem; outline: none; background: rgba(255,255,255,0.7); color: #3a5530; }
.f-grid input:focus, .f-grid select:focus { border-color: #7CB342; }
.f-btns { display: flex; gap: 6px; align-items: center; }
.btn-pri { border: none; background: #3B6B3B; color: #fff; padding: 6px 14px; border-radius: 6px; font-size: 0.8rem; cursor: pointer; }
.btn-ghost { border: 1px solid rgba(59,107,59,0.25); background: transparent; color: #3B6B3B; padding: 6px 14px; border-radius: 6px; font-size: 0.8rem; cursor: pointer; }
.err { margin: 6px 0 0; color: #c62828; font-size: 0.78rem; }
</style>
