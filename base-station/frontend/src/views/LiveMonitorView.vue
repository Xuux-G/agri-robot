<script setup>
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from "vue";
import { apiBase, clearSimulation, getFarmPlotDetail, getFarmPlots, getFarmRobot, getFarmStatus } from "../api/client.js";

const plots = ref([]);
const farmStatus = ref({ total: 0, normal: 0, warning: 0, danger: 0 });
const error = ref("");
const footerStatus = ref("scheduled");

// 处方比例
const prescription = computed(() => {
  const src = (isRobotHere.value && robotPrescription.value) ? robotPrescription.value
            : selectedPrescription.value ? selectedPrescription.value
            : robotPrescription.value;
  if (src) {
    return {
      medicineA: src.ratio_a || 0,
      medicineB: src.ratio_b || 0,
      water: src.water || 0,
      pesticide_a: src.pesticide_a || 'A药',
      pesticide_b: src.pesticide_b || '',
      ratio_label: src.ratio_label || '',
      severity: src.severity || '',
      locked: src.locked || false,
    };
  }
  return { medicineA: 0, medicineB: 0, water: 0, pesticide_a: '', pesticide_b: '', ratio_label: '', severity: '', locked: false };
});

// 选中地块
const selectedPlot = ref(null);
const selectedPrescription = ref(null);
const lastAutoOpenedImageAt = reactive({});

const BLOCK_RX = {
  "A-01": { pesticide_a: "阿维菌素", pesticide_b: "", ratio_a: 30, ratio_b: 0, water: 70, ratio_label: "3:7", severity: "严重", notes: "辣椒·菜青虫 — 重度虫害，单独喷洒阿维菌素，3:7配比", pest_type: "菜青虫" },
  "A-02": { pesticide_a: "吡唑醚菌酯", pesticide_b: "", ratio_a: 20, ratio_b: 0, water: 80, ratio_label: "2:8", severity: "轻度", notes: "茶树·白粉病 — 真菌病害，喷洒吡唑醚菌酯，2:8配比", pest_type: "白粉病", escalate: true },
  "A-03": { pesticide_a: "阿维菌素", pesticide_b: "吡唑醚菌酯", ratio_a: 17, ratio_b: 25, water: 58, ratio_label: "2:3:7", severity: "轻度", notes: "石榴·褐斑病+菜青虫 — 混合喷洒杀虫+杀菌，2:3:7", pest_type: "褐斑病+菜青虫" },
  "A-04": { pesticide_a: "四聚乙醛", pesticide_b: "", ratio_a: 30, ratio_b: 0, water: 70, ratio_label: "3:7", severity: "轻度", notes: "猕猴桃·蜗牛 — 杀螺剂喷洒，3:7配比，近期爆发严重", pest_type: "蜗牛", resolve: true },
  "A-05": { pesticide_a: "芸苔素内酯", pesticide_b: "", ratio_a: 20, ratio_b: 0, water: 80, ratio_label: "2:8", severity: "正常", notes: "山茶花·保健预防 — 区域病虫害高发预警，喷洒保健药2:8", pest_type: "", resolve: true },
  "A-06": { pesticide_a: "禁止施药", pesticide_b: "", ratio_a: 0, ratio_b: 0, water: 0, ratio_label: "0:0", severity: "轻度", notes: "柑橘·褐斑病PHI锁死 — 临近产期无适用药，人工复查", pest_type: "褐斑病", locked: true },
};

function selectPlot(plot) {
  selectedPlot.value = plot;
  selectedPrescription.value = BLOCK_RX[plot.block_id] || null;
}
function dismissDetail(rememberCurrent = false) {
  if (rememberCurrent && selectedPlot.value?.block_id) {
    lastAutoOpenedBlock.value = selectedPlot.value.block_id;
  }
  selectedPlot.value = null;
  selectedPrescription.value = null;
}
function closeDetail() {
  // 手动关闭后标记（用当前打开的区块而非机器人位置，避免任务完成后 currentRobotPlot 已为空导致标记失效）
  dismissDetail(true);
}

function getPlotByIndex(index) {
  const blockId = `A-${String(index).padStart(2, '0')}`;
  return plots.value.find((plot) => plot.block_id === blockId) || null;
}

// 模拟 STM32 上报区块信息（完全前端驱动）
const simActive = ref(false);
const simulatedBlocks = ref({}); // 记录本次模拟中被点击过的区块状态
const simResetAt = ref(0);
let simEventCounter = 100;
function persistSimSession() {
  if (simActive.value) {
    sessionStorage.setItem("agri_sim_active", "1");
    sessionStorage.setItem("agri_sim_status", footerStatus.value);
    sessionStorage.setItem("agri_sim_blocks", JSON.stringify(simulatedBlocks.value));
    if (currentRobotPlot.value) {
      sessionStorage.setItem("agri_sim_robot_plot", currentRobotPlot.value);
    } else {
      sessionStorage.removeItem("agri_sim_robot_plot");
    }
    return;
  }

  sessionStorage.removeItem("agri_sim_active");
  sessionStorage.removeItem("agri_sim_blocks");
  sessionStorage.removeItem("agri_sim_status");
  sessionStorage.removeItem("agri_sim_robot_plot");
}
function startSimMode() {
  simActive.value = true;
  markLiveActivity();
  simulatedBlocks.value = {};
  dismissDetail(false);
  currentRobotPlot.value = null;
  robotPrescription.value = null;
  persistSimSession();
  error.value = "";
  fetchData();
}
function completeSimulation() {
  if (!simActive.value || footerStatus.value === "completed") return;
  markTaskCompleted();
  dismissDetail(false);
  currentRobotPlot.value = null;
  robotPrescription.value = null;
  persistSimSession();
  fetchData();
}
async function resetSimulation() {
  markScheduled();
  simActive.value = false;
  simResetAt.value = Date.now();
  simulatedBlocks.value = {};
  persistSimSession();
  sessionStorage.setItem("agri_sim_reset_at", String(simResetAt.value));
  // 记录当前区块防止重置后被旧图片自动弹窗
  dismissDetail(true);
  currentRobotPlot.value = null;
  robotPrescription.value = null;
  await clearSimulation().catch(() => null);
  window.dispatchEvent(new CustomEvent("agri-sim-updated"));
  fetchData(); // 恢复初始状态
}
async function simulateBlockSelection(plot) {
  if (!simActive.value || footerStatus.value === "completed") return;
  const rx = BLOCK_RX[plot.block_id];
  if (!rx) return;
  markLiveActivity();

  // 1) 前端立即更新该区块状态
  const newStatus = rx.severity === "严重" ? "pest" : rx.severity === "轻度" ? "warning" : "normal";
  simulatedBlocks.value = { ...simulatedBlocks.value, [plot.block_id]: {
    status: newStatus,
    severity: rx.severity || "轻度",
    stateText: newStatus === "pest" ? "病害预警" : newStatus === "warning" ? "轻度异常" : "正常",
  }};
  persistSimSession();
  // 刷新六宫格显示
  fetchData();

  // 2) 后台发送模拟事件
  const ts = Date.now();
  const eventId = `nfc-block-${plot.block_id.replace('A-','')}-${ts}`;
  const pestType = rx.pest_type || rx.pesticide_a || "";
  try {
    await fetch(`${apiBase}/api/device/events`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        event_id: eventId,
        robot_id: "sim-robot-01",
        block_id: plot.block_id,
        event_type: "nfc_detected",
        status: "running",
        severity: rx.severity || "轻度",
        pest_type: pestType,
        action_type: "spray",
        crop_type: plot.crop_type,
        device_time: new Date().toISOString(),
      }),
    }).then(r => r.json()).catch(() => null);
  } catch (_) {}

  // 6) 通知农田管理面板同步刷新
  window.dispatchEvent(new CustomEvent("agri-sim-updated"));

  // 3) 更新机器人位置和处方
  currentRobotPlot.value = plot.block_id;
  robotPrescription.value = rx;
  persistSimSession();
  // 4) 打开该区块详情
  selectPlot(plot);
}

function handleSimHotkey(event) {
  const target = event.target;
  const tagName = target?.tagName?.toLowerCase?.() || "";
  const isTyping = target?.isContentEditable || ["input", "textarea", "select"].includes(tagName);
  if (isTyping) return;

  if (event.key === "Enter") {
    event.preventDefault();
    if (!simActive.value) startSimMode();
    else if (footerStatus.value === "running") completeSimulation();
    else resetSimulation();
    return;
  }

  if (simActive.value && event.key === "Escape") {
    event.preventDefault();
    resetSimulation();
    return;
  }

  const keyMap = {
    "1": 1,
    "2": 2,
    "3": 3,
    "4": 4,
    "5": 5,
    "6": 6,
    Numpad1: 1,
    Numpad2: 2,
    Numpad3: 3,
    Numpad4: 4,
    Numpad5: 5,
    Numpad6: 6,
  };
  const plotIndex = keyMap[event.key];
  if (!plotIndex) return;

  event.preventDefault();
  if (!simActive.value) {
    startSimMode();
  }
  if (footerStatus.value === "completed") return;

  const plot = getPlotByIndex(plotIndex);
  if (plot) {
    simulateBlockSelection(plot);
  }
}

// 知识库速查
const pestQuickRef = {
  "菜青虫": { name: "菜青虫", engName: "Cabbage Worm / Pieris rapae", category: "咀嚼式害虫", phi: 7, symptom: "幼虫啃食叶片成孔洞缺刻，严重时仅剩叶脉，排泄物污染菜心", control: "阿维菌素乳油2000倍液喷雾", image: "/uploads/caiqingchong.jpg" },
  "蜗牛": { name: "蜗牛", engName: "Snail / Gastropoda", category: "软体动物", phi: 14, symptom: "齿舌刮食叶片及果实表皮，形成孔洞与银色黏液痕迹，潮湿季节爆发", control: "四聚乙醛颗粒撒施或喷雾", image: "/uploads/woniu.jpg" },
  "白粉病": { name: "白粉病", engName: "Powdery Mildew", category: "真菌性病害", phi: 7, symptom: "叶片表面出现白色粉状霉层，严重时叶片变黄枯死", control: "吡唑醚菌酯悬浮剂2000倍液喷雾", image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/3d1401288196ada1be792b8d9ea968be.jpg" },
  "褐斑病": { name: "褐斑病", engName: "Brown Spot", category: "真菌性病害", phi: 14, symptom: "叶片出现褐色圆形或不规则病斑，边缘深褐色，中部灰白色", control: "苯醚甲环唑2000倍液", image: "/uploads/heban_demo.jpg" },
  "褐斑病+菜青虫": { name: "褐斑病+菜青虫", engName: "Brown Spot + Cabbage Worm", category: "混合病虫害", phi: 14, symptom: "褐斑病斑与虫咬孔洞同时出现，叶片双重受损", control: "阿维菌素+吡唑醚菌酯混合喷洒，配比2:3:7", image: "/uploads/heban_demo.jpg" },
  "蚜虫": { name: "蚜虫", engName: "Aphid", category: "刺吸式害虫", phi: 7, symptom: "群集嫩叶嫩芽吸食汁液，致叶片卷曲变形", control: "吡虫啉可湿性粉剂2000倍液", image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/98c707ad11f6e0f4a212d7699c6c50ab.jpg" },
  "甜菜夜蛾": { name: "甜菜夜蛾", engName: "Beet Armyworm", category: "咀嚼式害虫", phi: 10, symptom: "幼虫取食叶片形成孔洞", control: "甲维盐乳油2000倍液", image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/f12ebe178bfb0306522ff0d33d95ff1e.jpg" },
  "霜霉病": { name: "霜霉病", engName: "Downy Mildew", category: "真菌性病害", phi: 10, symptom: "叶背产生白色霜状霉层", control: "霜脲·锰锌可湿性粉剂600倍液", image: "" },
  "灰霉病": { name: "灰霉病", engName: "Gray Mold", category: "真菌性病害", phi: 7, symptom: "产生灰色霉层，果实变软腐烂", control: "嘧霉胺悬浮剂1000倍液", image: "" },
};
const pesticideMechanisms = {
  "阿维菌素": "特异作用于害虫神经传导，使其持续麻痹、拒食并最终死亡；对螨类和咀嚼式害虫兼具触杀、胃毒作用，但通常不直接杀卵",
  "吡唑醚菌酯": "抑制病菌线粒体呼吸与能量合成，阻断孢子萌发、菌丝扩展和病斑继续蔓延，偏重预防与早期控制",
  "四聚乙醛": "破坏蜗牛体表黏液平衡并促进失水，抑制其爬行和取食，最终使其麻痹死亡",
  "芸苔素内酯": "通过调节作物体内生理代谢和抗逆反应，促进恢复长势，提高抗逆性，属于生长调节型保健方案",
};
const pestEnvironmentCauses = {
  "菜青虫": "成因：多发于温暖季节、寄主作物连片种植、杂草较多或虫口基数偏高的环境。",
  "白粉病": "成因：昼夜温差大、通风不足、植株郁闭或空气湿度波动时容易发生。",
  "褐斑病": "成因：高湿、多露水、叶面长时间带水及通风不良时更易扩展。",
  "蜗牛": "成因：阴雨潮湿、地表有机残体多、庇荫重且土壤湿度偏高时容易爆发。",
};
const pestInfoOverlay = ref(null);
function showPestInfo(name) { pestInfoOverlay.value = pestQuickRef[name] || null; }

const currentRobotPlot = ref(null);
const robotPrescription = ref(null);
const lastAutoOpenedBlock = ref("");

const TOTAL_BLOCKS = 20;
const redBlocks = computed(() => {
  return Math.round(prescription.value.medicineA / 100 * TOTAL_BLOCKS);
});
const yellowBlocks = computed(() => {
  if (!prescription.value.pesticide_b) return 0;
  return Math.round(prescription.value.medicineB / 100 * TOTAL_BLOCKS);
});
const blueBlocks = computed(() => TOTAL_BLOCKS - redBlocks.value - yellowBlocks.value);
const detailPest = computed(() => {
  if (!selectedPlot.value?.pest_type) return null;
  return pestQuickRef[selectedPlot.value.pest_type] || null;
});
const detailCauseText = computed(() => {
  if (prescription.value.locked) {
    return '形成环境：当前主要受采收期和安全间隔限制影响，需人工复查后再决定处置。';
  }
  if (!selectedPlot.value?.pest_type) return '';

  const causes = selectedPlot.value.pest_type
    .split('+')
    .map((name) => name.trim())
    .map((name) => ({ name, cause: pestEnvironmentCauses[name] }))
    .filter((item) => item.cause);

  if (!causes.length) return '';
  return `形成环境：${causes.map((item) => `${item.name}多见于${item.cause.replace(/^成因：/, '').replace(/。$/, '')}`).join('；')}。`;
});
// 拆分复合病虫害（如 褐斑病+菜青虫 → [褐斑病, 菜青虫]），各自取知识库图片
const detailPestImages = computed(() => {
  if (!selectedPlot.value?.pest_type) return [];
  const types = selectedPlot.value.pest_type.split('+').map(s => s.trim());
  return types.map(t => pestQuickRef[t]).filter(Boolean);
});
const selectedCameraImage = computed(() => {
  const imageUrl = selectedPlot.value?.camera_image || "";
  const pestType = selectedPlot.value?.pest_type || "";
  if (!imageUrl) return "";
  if (!pestType || ["猕猴桃", "橘子", "mihoutao", "juzi"].includes(pestType)) return "";
  return imageUrl;
});
const mixStep = ref(0);
let mixTimer = null;
const isRobotHere = computed(() => selectedPlot.value?.block_id === currentRobotPlot.value);
const abnormalPlotCount = computed(() => plots.value.filter((plot) => plot.status === "warning" || plot.status === "pest").length);
const todayTaskDate = computed(() => {
  const now = new Date();
  const month = now.getMonth() + 1;
  const day = now.getDate();
  return `${month}月${day}日`;
});
const footerLiveText = computed(() => {
  if (footerStatus.value === "running") return "系统在线运行中";
  if (footerStatus.value === "completed") return "当期作业任务已完成";
  return `下次作业时间为 ${todayTaskDate.value}`;
});

function markLiveActivity() {
  footerStatus.value = "running";
}

function markTaskCompleted() {
  footerStatus.value = "completed";
}

function markScheduled() {
  footerStatus.value = "scheduled";
}

const aiAdviceText = computed(() => {
  const p = prescription.value;
  if (p.locked) return '临近产期，等待人工复查';
  const parts = [];
  if (p.severity === '严重') parts.push('当前为重度防治方案，');
  else if (p.severity === '轻度') parts.push('当前为轻度防治方案，');
  else parts.push('当前为保健预防方案，');
  if (p.pesticide_a && p.pesticide_b) {
    parts.push(`采用「${p.pesticide_a} + ${p.pesticide_b}」混合喷洒，配比 ${p.ratio_label}。`);
  } else if (p.pesticide_a) {
    parts.push(`采用「${p.pesticide_a}」单独喷洒，配比 ${p.ratio_label}。`);
  }
  return parts.join('');
});

const aiMechanismText = computed(() => {
  const p = prescription.value;
  if (p.locked) return '机理：当前处于安全间隔或产期限制阶段，本次不执行施药。';

  const pesticides = [p.pesticide_a, p.pesticide_b].filter(Boolean);

  const mechanisms = pesticides
    .map((name, index) => {
      const mechanism = pesticideMechanisms[name];
      if (!mechanism) return '';
      return `${name}机理：${mechanism}`;
    })
    .filter(Boolean);

  return mechanisms.length ? `${mechanisms.join('；')}。` : '';
});

const mixBlocks = computed(() => {
  const blocks = [];
  // 禁止施药：全空瓶
  if (prescription.value.locked) {
    for (let i = 0; i < TOTAL_BLOCKS; i++) {
      blocks.push({ color: 'transparent', filled: false });
    }
    return blocks;
  }
  const rb = redBlocks.value, yb = yellowBlocks.value, bb = blueBlocks.value;
  for (let i = 0; i < TOTAL_BLOCKS; i++) {
    let color = '';
    if (i < rb) color = '#ef5350';
    else if (i < rb + yb) color = '#ffb300';
    else color = '#42a5f5';
    blocks.push({ color, filled: i < mixStep.value });
  }
  return blocks;
});

function startMixAnim() {
  stopMixAnim();
  mixStep.value = 0;
  mixTimer = setInterval(() => {
    if (mixStep.value < TOTAL_BLOCKS) {
      mixStep.value++;
    } else {
      clearInterval(mixTimer);
      mixTimer = setTimeout(() => { mixStep.value = 0; startMixAnim(); }, 2200);
    }
  }, 120);
}
function stopMixAnim() {
  if (mixTimer) { clearInterval(mixTimer); clearTimeout(mixTimer); mixTimer = null; }
}

watch(selectedPlot, (plot, oldPlot) => {
  const prevBlock = oldPlot?.block_id ?? null;
  const newBlock = plot?.block_id ?? null;
  // 同一区块的浅层刷新（如 fetchData 覆写对象引用）不干预动画
  if (newBlock === prevBlock) return;
  stopMixAnim();
  if (plot && plot.block_id === currentRobotPlot.value) {
    startMixAnim();
  } else {
    mixStep.value = 0;  // 清空配药柱
  }
});

let pollTimer = null;
// 初始展示状态（非模拟模式下固定展示）
const INITIAL_STATES = {
  "A-01": { status: "warning", severity: "轻度", stateText: "轻度异常" },
  "A-02": { status: "normal", severity: "正常", stateText: "正常" },
  "A-03": { status: "normal", severity: "正常", stateText: "正常" },
  "A-04": { status: "pest", severity: "严重", stateText: "病害预警" },
  "A-05": { status: "warning", severity: "轻度", stateText: "轻度异常" },
  "A-06": { status: "normal", severity: "正常", stateText: "正常" },
};

async function fetchData() {
  try {
    const [pd, sd, rd] = await Promise.all([
      getFarmPlots(),
      getFarmStatus(),
      getFarmRobot().catch(() => null),
    ]);
    const isRealHub = rd?.robot_id === "esp32-s3-hub-001";
    const realUpdatedAt = rd?.updated_at ? new Date(rd.updated_at).getTime() : 0;
    const realEventFresh = realUpdatedAt && (Date.now() - realUpdatedAt) < 20000;
    const realRobotBlock = isRealHub && realEventFresh && rd?.block
      ? `A-${String(rd.block).padStart(2, '0')}`
      : null;
    plots.value = (pd.items || []).map((item) => {
      const init = INITIAL_STATES[item.block_id] || { status: "normal", severity: "正常", stateText: "正常" };
      // 模拟模式下，被点击过的区块用模拟状态，否则用初始状态
      const sim = simActive.value ? simulatedBlocks.value[item.block_id] : null;
      const rx = BLOCK_RX[item.block_id] || {};
      const isFruitImage = ["猕猴桃", "橘子", "mihoutao", "juzi", "none", ""].includes(item.pest_type || "");
      // 区块有真实病虫害图片（且图片时间晚于上次重置）→ 用 API 的 governance/severity，不随机器人离开而消失
      const imageTime = item.image_created_at ? new Date(item.image_created_at).getTime() : 0;
      const imageFresh = imageTime && (!simResetAt.value || imageTime > simResetAt.value);
      const hasRealPestData = item.image_url && !isFruitImage && imageFresh;
      const real = hasRealPestData ? {
        status: item.governance === "严重" || item.governance === "病害预警"
          ? "pest"
          : item.governance === "轻度" || item.governance === "轻度异常"
            ? "warning"
            : "normal",
        severity: item.severity || rx.severity || "正常",
        stateText: item.governance === "严重" || item.governance === "病害预警"
          ? "病害预警"
          : item.governance === "轻度" || item.governance === "轻度异常"
            ? "轻度异常"
            : "正常",
      } : null;
      // 作业完成后也使用固定结果映射，与电子药历治理状态一一对应：
      // 01严重；02/03/04/06轻度(A06显示待人工复查)；05正常
      // 仅在任务刚刚完成(footerStatus=completed)时生效，避免历史数据覆盖初始态
      const doneState = !sim && footerStatus.value === "completed" ? {
        status: item.block_id === "A-01" ? "pest" : item.block_id === "A-05" ? "normal" : "warning",
        severity: rx.severity || "轻度",
        stateText:
          item.block_id === "A-06"
            ? "待人工复查"
            : item.block_id === "A-01"
              ? "病害预警"
              : item.block_id === "A-05"
                ? "正常"
                : "轻度异常",
      } : null;
      const effective = sim || doneState || real || init;
      return {
        id: item.block_id, block_id: item.block_id,
        status: effective.status,
        crop_type: item.crop_type || "未知作物", severity: effective.severity,
        device_status: sim || realRobotBlock === item.block_id ? "running" : "idle",
        temperature: item.temperature, humidity: item.humidity,
        pest_type: item.pest_type || rx.pest_type || "",
        camera_image: item.image_url || "",
        camera_image_at: item.image_created_at || "",
        stateText: effective.stateText,
      };
    });
    for (const plot of plots.value) {
      if (!lastAutoOpenedImageAt[plot.block_id] && plot.camera_image_at) {
        lastAutoOpenedImageAt[plot.block_id] = plot.camera_image_at;
      }
    }
    if (selectedPlot.value?.block_id) {
      const refreshedSelectedPlot = plots.value.find((plot) => plot.block_id === selectedPlot.value.block_id);
      // 仅当区块/状态确实变化时才更新引用，避免触发 watch 杀死混药动画
      if (refreshedSelectedPlot && (
        refreshedSelectedPlot.status !== selectedPlot.value.status ||
        refreshedSelectedPlot.pest_type !== selectedPlot.value.pest_type ||
        refreshedSelectedPlot.camera_image !== selectedPlot.value.camera_image
      )) {
        selectedPlot.value = refreshedSelectedPlot;
      }
    }
    farmStatus.value = { total: sd.total ?? 0, normal: sd.normal ?? 0, warning: sd.warning ?? 0, danger: sd.danger ?? 0 };

    // 真实作业链路：先移动小车，再在图片区块成熟后自动打开详情。
    const realBlock = isRealHub && rd.block ? `A-${String(rd.block).padStart(2, '0')}` : null;
    const prevRealBlock = currentRobotPlot.value;
    const realCompleted = isRealHub && rd?.status === "completed";

    if (realCompleted && realBlock === "A-06") {
      // 只关闭自动打开的 A-06 详情，不关用户手动点开的其他区块
      if (selectedPlot.value?.block_id === "A-06" && lastAutoOpenedBlock.value === "A-06") {
        dismissDetail(false);
      }
      currentRobotPlot.value = null;
      robotPrescription.value = null;
      lastAutoOpenedBlock.value = "";
      if (!simActive.value) markTaskCompleted();
    } else if (realBlock) {
      if (prevRealBlock && prevRealBlock !== realBlock && selectedPlot.value?.block_id === prevRealBlock) {
        dismissDetail(false);
      }
      currentRobotPlot.value = realBlock;
      if (!simActive.value) markLiveActivity();
    }

    // 处方（独立于详情打开条件）
    if (realBlock) {
      if (rd.prescription && rd.prescription.ratio_a > 0) {
        robotPrescription.value = rd.prescription;
      } else if (BLOCK_RX[realBlock]) {
        robotPrescription.value = BLOCK_RX[realBlock];
      }
    }

    // 只有图片区块到位后才自动打开详情；completed 态不再自动推进。
    const allowRealOpen = (
      realBlock &&
      !realCompleted &&
      (!simResetAt.value || realUpdatedAt > simResetAt.value)
    );
    if (allowRealOpen) {
      const robotPlot = plots.value.find((plot) => plot.block_id === realBlock);
      const blockChanged = prevRealBlock !== realBlock;
      if (blockChanged) {
        lastAutoOpenedBlock.value = "";
      }
      const lastOpenedAt = lastAutoOpenedImageAt[realBlock] || "";
      const imageIsNew = robotPlot?.camera_image_at && robotPlot.camera_image_at > lastOpenedAt;
      if (robotPlot?.camera_image && imageIsNew && selectedPlot.value?.block_id !== realBlock && lastAutoOpenedBlock.value !== realBlock) {
        lastAutoOpenedImageAt[realBlock] = robotPlot.camera_image_at;
        lastAutoOpenedBlock.value = realBlock;
        selectPlot(robotPlot);
      }
    }
    error.value = "";
  } catch (err) { error.value = "作业监控数据加载失败"; }
}

onMounted(() => {
  // 恢复模拟状态（切换页面不丢失）
  if (sessionStorage.getItem('agri_sim_active') === '1') {
    simActive.value = true;
    const savedStatus = sessionStorage.getItem('agri_sim_status');
    if (savedStatus) footerStatus.value = savedStatus;
    const savedRobot = sessionStorage.getItem('agri_sim_robot_plot');
    if (savedRobot) currentRobotPlot.value = savedRobot;
    try {
      const saved = sessionStorage.getItem('agri_sim_blocks');
      if (saved) simulatedBlocks.value = JSON.parse(saved);
    } catch (_) {}
  } else {
    const savedResetAt = Number(sessionStorage.getItem('agri_sim_reset_at') || 0);
    if (savedResetAt) simResetAt.value = savedResetAt;
  }
  fetchData();
  pollTimer = setInterval(fetchData, 1200);
  window.addEventListener('keydown', handleSimHotkey);
});
onBeforeUnmount(() => {
  stopMixAnim();
  if (pollTimer) clearInterval(pollTimer);
  window.removeEventListener('keydown', handleSimHotkey);
});
</script>

<template>
  <div class="monitor-page">
    <!-- 标题区 -->
    <div class="monitor-top">
      <div>
        <h2>农田数字孪生指挥系统</h2>
        <p>点击地块查看详情与处方 — 实时刷新</p>
      </div>
      <div class="legend-row">
        <div class="legend-item"><span class="dot green"></span> 正常</div>
        <div class="legend-item"><span class="dot amber"></span> 预警</div>
        <div class="legend-item"><span class="dot red"></span> 虫害</div>
        <div class="legend-item"><span class="dot blue ping"></span> 作业中</div>
      </div>
    </div>

    <p v-if="error" class="err-line">{{ error }}</p>

    <!-- 6宫格 - 恢复大格 -->
    <div class="grid-6">
      <div
        v-for="plot in plots" :key="plot.id"
        :class="['plot-cell', plot.status === 'pest' ? 'cell-pest' : plot.status === 'warning' ? 'cell-warn' : 'cell-ok']"
        @click="simActive && footerStatus !== 'completed' ? simulateBlockSelection(plot) : selectPlot(plot)"
      >
        <div class="cell-grain"></div>
        <div class="cell-top"><div class="cell-id">{{ plot.block_id }}</div><div class="cell-crop">{{ plot.crop_type }}</div></div>
        <div class="cell-state">
          <div class="state-icon">
            <svg v-if="plot.status==='pest'" width="28" height="28" viewBox="0 0 48 48" fill="none"><path d="M24 4L44 40H4L24 4z" stroke="currentColor" stroke-width="2" fill="none"/><line x1="24" y1="18" x2="24" y2="28" stroke="currentColor" stroke-width="2.5"/><circle cx="24" cy="34" r="1.5" fill="currentColor"/></svg>
            <svg v-else-if="plot.status==='warning'" width="24" height="24" viewBox="0 0 48 48" fill="none"><circle cx="24" cy="24" r="20" stroke="currentColor" stroke-width="2.5"/><line x1="24" y1="14" x2="24" y2="28" stroke="currentColor" stroke-width="2.5"/><circle cx="24" cy="34" r="1.5" fill="currentColor"/></svg>
            <svg v-else width="22" height="22" viewBox="0 0 48 48" fill="none"><path d="M14 28c3-2 6-6 8-10 2-4 8-2 6 3s-6 10-14 7z" stroke="currentColor" stroke-width="2"/></svg>
          </div>
          <div class="state-text">{{ plot.status==='pest'?'病害预警':plot.status==='warning'?'轻度异常':'正常' }}</div>
          <div v-if="plot.block_id==='A-06' && plot.status==='warning'" class="state-sub">待人工复查</div>
        </div>
        <div v-if="currentRobotPlot===plot.block_id" class="robot-badge">
          <div class="radar-ring"></div>
          <div class="robot-icon"><svg width="18" height="18" viewBox="0 0 48 48" fill="none"><rect x="8" y="14" width="32" height="24" rx="4" stroke="currentColor" stroke-width="2.5"/><circle cx="16" cy="38" r="5" stroke="currentColor" stroke-width="2.5"/><circle cx="32" cy="38" r="5" stroke="currentColor" stroke-width="2.5"/></svg></div>
        </div>
      </div>
    </div>

    <!-- 全屏虚化详情弹窗 -->
    <div v-if="selectedPlot" class="detail-overlay" @click.self="closeDetail">
      <div class="detail-modal">
        <button class="dm-close" @click="closeDetail">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
        </button>
        <div class="dm-header">
          <h2>{{ selectedPlot.block_id }} · {{ selectedPlot.crop_type }}</h2>
          <span :class="['dm-badge', selectedPlot.status === 'pest' ? 'badge-red' : selectedPlot.status === 'warning' ? 'badge-amber' : 'badge-green']">
            {{ selectedPlot.status === 'pest' ? '病害预警' : selectedPlot.status === 'warning' ? '轻度异常' : '正常' }}
          </span>
        </div>
        <div class="dm-body">
          <div class="dm-left">
            <!-- 图片区：按病虫害拆分，每种病害/害虫各一张知识库图 -->
            <div v-if="detailPestImages.length || selectedCameraImage" class="dm-images">
              <div v-for="(pest, i) in detailPestImages" :key="'kb-' + i" class="dm-img-wrap">
                <img :src="pest.image" class="dm-img" />
                <div class="dm-img-tag">{{ pest.name }}</div>
              </div>
              <div v-if="selectedCameraImage" class="dm-img-wrap">
                <img :src="selectedCameraImage" class="dm-img" />
                <div class="dm-img-tag live">近期实况</div>
              </div>
            </div>
            <div v-if="detailPest" class="dm-pest-info">
              <div>
                <strong>{{ detailPest.name }}</strong>
                <button class="dm-pest-detail-btn" @click.stop="showPestInfo(detailPest.name)">详细</button>
                <p class="dm-eng">{{ detailPest.engName }}</p>
                <p class="dm-symptom">{{ detailPest.symptom }}</p>
                <p v-if="detailCauseText" class="dm-cause">{{ detailCauseText }}</p>
                <p class="dm-ctrl">{{ detailPest.control }}</p>
                <span v-if="detailPest.phi" class="dm-phi">PHI {{ detailPest.phi }}天</span>
              </div>
            </div>
            <div v-else-if="selectedPrescription && selectedPrescription.severity !== '正常'" class="dm-pest-info">
              <div>
                <strong>{{ selectedPlot.block_id }} 处方方案</strong>
                <p class="dm-eng">{{ selectedPrescription.notes }}</p>
                <p class="dm-symptom">配比：{{ selectedPrescription.pesticide_a }}{{ selectedPrescription.pesticide_b ? ' + ' + selectedPrescription.pesticide_b : '' }}，{{ selectedPrescription.ratio_label }}</p>
                <p v-if="detailCauseText" class="dm-cause">{{ detailCauseText }}</p>
                <p class="dm-ctrl">{{ selectedPrescription.pesticide_a }} {{ selectedPrescription.ratio_a }}%{{ selectedPrescription.pesticide_b ? ' · ' + selectedPrescription.pesticide_b + ' ' + selectedPrescription.ratio_b + '%' : '' }} · 水 {{ selectedPrescription.water }}%</p>
                <span v-if="selectedPrescription.locked" class="dm-phi" style="background:linear-gradient(135deg,#fff3e0,#ffe0b2);color:#e65100;padding:6px 12px;border-radius:6px;display:inline-block;margin-top:8px;font-size:0.78rem;font-weight:600;">⚠️ 存疑待人工复查</span>
              </div>
            </div>
            <div v-else class="dm-normal">
              <svg width="36" height="36" viewBox="0 0 48 48" fill="none"><path d="M14 28c3-2 6-6 8-10 2-4 8-2 6 3s-6 10-14 7z" stroke="#7CB342" stroke-width="2"/></svg>
              <p>该地块当前状态正常，暂无病害信息</p>
            </div>
          </div>
          <div class="dm-right">
            <h4>处方决策 / 智能配药</h4>
            <div class="dm-mix">
              <div class="dm-cyl" :class="{ 'cyl-animating': isRobotHere }">
                <div class="dm-cyl-inner">
                  <div
                    v-for="(blk, idx) in mixBlocks" :key="idx"
                    class="dm-blk"
                    :style="{ background: blk.filled ? blk.color : 'transparent' }"
                  ></div>
                </div>
                <div class="dm-cyl-label locked" v-if="prescription.locked">禁止施药</div>
                <div class="dm-cyl-label empty-bottle" v-if="prescription.locked">
                  <svg width="24" height="24" viewBox="0 0 48 48" fill="none" stroke="rgba(198,40,40,0.4)" stroke-width="2">
                    <rect x="16" y="4" width="16" height="40" rx="4"/>
                    <line x1="20" y1="10" x2="28" y2="10"/>
                    <line x1="20" y1="14" x2="24" y2="14"/>
                  </svg>
                </div>
                <div class="dm-cyl-label" v-else-if="isRobotHere">混药中···</div>
              </div>
              <div class="dm-stack-wrap">
                <div class="dm-stack-label">{{ prescription.pesticide_a }} {{ prescription.medicineA }}%<template v-if="prescription.pesticide_b"> · {{ prescription.pesticide_b }} {{ prescription.medicineB }}%</template> · 水 {{ prescription.water }}%</div>
                <div class="dm-stack-bar">
                  <div :style="{ width: `${prescription.medicineA}%` }" style="background:#ef5350"></div>
                  <div v-if="prescription.pesticide_b" :style="{ width: `${prescription.medicineB}%` }" style="background:#ffb300"></div>
                  <div :style="{ width: `${prescription.water}%` }" style="background:#42a5f5"></div>
                </div>
                <div class="dm-pumps" :class="{ 'pumps-two': !prescription.pesticide_b }">
                  <div class="dm-pump"><span>{{ prescription.pesticide_a || 'A药' }}</span><strong>{{ prescription.medicineA }}%</strong></div>
                  <div v-if="prescription.pesticide_b" class="dm-pump"><span>{{ prescription.pesticide_b }}</span><strong>{{ prescription.medicineB }}%</strong></div>
                  <div class="dm-pump"><span>清水</span><strong>{{ prescription.water }}%</strong></div>
                </div>
              </div>
            </div>
            <p class="dm-ai-text">{{ aiAdviceText }}</p>
            <p v-if="aiMechanismText" class="dm-ai-mechanism">{{ aiMechanismText }}</p>
          </div>
        </div>
      </div>
    </div>

    <!-- 知识库弹窗 -->
    <div v-if="pestInfoOverlay" class="pest-info-overlay" @click.self="pestInfoOverlay=null">
      <div class="pest-info-card">
        <button class="pest-info-close" @click="pestInfoOverlay=null">
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
        </button>
        <div class="pest-info-body">
          <img v-if="pestInfoOverlay.image" :src="pestInfoOverlay.image" class="pest-info-img" />
          <h4>{{ pestInfoOverlay.name }}</h4>
          <p class="pest-info-eng">{{ pestInfoOverlay.engName }}</p>
          <span class="pest-info-cat">{{ pestInfoOverlay.category }}</span>
          <div class="pest-info-sections">
            <div><strong>症状</strong><p>{{ pestInfoOverlay.symptom }}</p></div>
            <div><strong>防治</strong><p>{{ pestInfoOverlay.control }}</p></div>
          </div>
          <div v-if="pestInfoOverlay.phi" class="pest-info-phi">安全间隔期 {{ pestInfoOverlay.phi }} 天</div>
        </div>
      </div>
    </div>

    <!-- 底部状态栏 -->
    <div class="monitor-footer">
      <div class="footer-stats">
        <span>当前作业区块：<strong>{{ currentRobotPlot }}</strong></span>
        <span>已接入地块：<strong>{{ farmStatus.total }} 个</strong></span>
        <span>异常地块：<strong class="red">{{ abnormalPlotCount }} 个</strong></span>
      </div>
      <div class="footer-live"><span class="live-dot"></span> {{ footerLiveText }}</div>
    </div>
  </div>
</template>

<style scoped>
.monitor-page {
  height: 100%; display: flex; flex-direction: column; gap: 10px; padding: 20px; overflow: auto;
  background:
    url("data:image/svg+xml,%3Csvg viewBox='0 0 200 200' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.7' numOctaves='3' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)' opacity='0.025'/%3E%3C/svg%3E"),
    linear-gradient(180deg, #f5efe0 0%, #e8f0e0 25%, #d4e8c8 45%, #c5d8b0 60%, #b8c8a0 78%, #9aaf82 92%, #8a9e78 100%);
  border-radius: 16px; border: 1px solid rgba(59,107,59,0.06);
}
.monitor-top { display: flex; align-items: flex-end; justify-content: space-between; padding-bottom: 8px; border-bottom: 1px solid rgba(59,107,59,0.1); }
.monitor-top h2 { margin: 0; font-size: 1.4rem; color: #2d5a27; }
.monitor-top p { margin: 2px 0 0; font-size: 0.82rem; color: #7a9670; }
.legend-row { display: flex; gap: 14px; font-size: 0.82rem; color: #5a7d4a; align-items: center; }
.legend-item { display: flex; align-items: center; gap: 4px; }
.dot { width: 8px; height: 8px; border-radius: 50%; }
.dot.green { background: #66bb6a; } .dot.amber { background: #ffa726; } .dot.red { background: #ef5350; } .dot.blue { background: #42a5f5; }
.dot.ping { animation: ping 1.5s infinite; }
@keyframes ping { 0%,100%{opacity:1} 50%{opacity:0.35} }
.sim-btn { border: 1px dashed rgba(59,107,59,0.35); background: rgba(255,255,255,0.5); color: #3B6B3B; font-size: 0.7rem; padding: 3px 10px; border-radius: 6px; cursor: pointer; margin-left: 8px; }
.sim-btn:hover { background: rgba(59,107,59,0.08); border-color: #3B6B3B; }
.sim-btn.reset { border-color: rgba(198,40,40,0.3); color: #c62828; }
.err-line { margin: 0; color: #c62828; font-size: 0.75rem; }

/* 6宫格 - 固定大小不随宽度拉伸 */
.grid-6 {
  display: grid; grid-template-columns: repeat(3, minmax(180px, 280px)); gap: 12px;
  justify-content: center; align-content: start;
}
.plot-cell {
  aspect-ratio: 1; border-radius: 14px; position: relative; overflow: hidden; border: 2px solid transparent;
  transition: all 0.25s; cursor: pointer; width: 100%; max-width: 280px;
}
.plot-cell:hover { transform: scale(1.02); z-index: 2; }
.plot-cell.selected { border-color: #ffd54f; box-shadow: 0 0 0 3px rgba(255,213,79,0.3); }
.cell-ok { background: linear-gradient(135deg, #a5d6a7, #66bb6a); }
.cell-warn { background: linear-gradient(135deg, #ffe0b2, #ffb74d); }
.cell-pest { background: linear-gradient(135deg, #ef9a9a, #ef5350); }
.cell-grain { position: absolute; inset: 0; opacity: 0.12;
  background-image: linear-gradient(to right, rgba(255,255,255,0.3) 1px, transparent 1px), linear-gradient(to bottom, rgba(255,255,255,0.3) 1px, transparent 1px); background-size: 16px 16px; }
.cell-top { position: absolute; top: 6px; left: 8px; display: flex; gap: 6px; align-items: center; z-index: 1; }
.cell-id { background: rgba(0,0,0,0.3); color: #fff; font-size: 0.95rem; font-weight: 700; padding: 2px 8px; border-radius: 3px; }
.cell-crop { font-size: 1.05rem; color: rgba(255,255,255,0.85); }
.cell-state { position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; color: #fff; }
.state-icon { margin-bottom: 2px; }
.state-text { font-size: 0.95rem; font-weight: 600; }
.state-sub { font-size: 0.62rem; font-weight: 500; opacity: 0.8; margin-top: 2px; color: #ffcc80; }
.robot-badge { position: absolute; bottom: 4px; right: 6px; display: flex; align-items: center; gap: 4px; background: rgba(0,0,0,0.45); color: #fff; font-size: 0.72rem; padding: 2px 8px; border-radius: 12px; }
.radar-ring { position: absolute; inset: -6px; border-radius: 50%; border: 2px solid rgba(255,255,255,0.4); animation: ping 1.2s infinite; }
.robot-icon { display: flex; }

/* 全屏虚化详情弹窗 */
.detail-overlay {
  position: fixed; inset: 0; background: rgba(0,0,0,0.45); z-index: 100;
  display: flex; align-items: center; justify-content: center;
  backdrop-filter: blur(6px);
}
.detail-modal {
  background: #fff; border-radius: 20px; max-width: 920px; width: 92%; max-height: 88vh;
  overflow: auto; box-shadow: 0 20px 60px rgba(0,0,0,0.25);
  padding: 32px 36px; position: relative;
}
.dm-close { position: absolute; top: 16px; right: 16px; border: none; background: rgba(0,0,0,0.06); border-radius: 50%; width: 36px; height: 36px; display: flex; align-items: center; justify-content: center; cursor: pointer; color: #666; }
.dm-header { display: flex; align-items: center; gap: 14px; margin-bottom: 20px; }
.dm-header h2 { margin: 0; font-size: 1.35rem; color: #2d5a27; }
.dm-badge { font-size: 0.78rem; padding: 4px 14px; border-radius: 20px; font-weight: 600; }
.badge-red { background: rgba(239,83,80,0.1); color: #c62828; }
.badge-amber { background: rgba(255,167,38,0.1); color: #e65100; }
.badge-green { background: rgba(102,187,106,0.1); color: #2e7d32; }
.dm-body { display: grid; grid-template-columns: 1fr 1fr; gap: 28px; }
.dm-left { display: flex; flex-direction: column; gap: 12px; }
/* 图片区 */
.dm-images { display: flex; gap: 10px; }
.dm-img-wrap { flex: 1; position: relative; }
.dm-img { width: 100%; height: 140px; object-fit: cover; border-radius: 10px; border: 1px solid rgba(59,107,59,0.1); display: block; }
.dm-img-tag { position: absolute; bottom: 6px; left: 6px; font-size: 0.58rem; background: rgba(0,0,0,0.45); color: #fff; padding: 1px 7px; border-radius: 3px; }
.dm-img-tag.live { background: rgba(198,40,40,0.65); }
.dm-pest-info { display: flex; gap: 0; flex-direction: column; }
.dm-left strong { font-size: 1rem; color: #2d5a27; }
.dm-pest-detail-btn { margin-left: 8px; font-size: 0.7rem; padding: 2px 10px; border: 1px solid #3B6B3B; border-radius: 12px; background: rgba(59,107,59,0.06); color: #3B6B3B; cursor: pointer; vertical-align: middle; }
.dm-pest-detail-btn:hover { background: rgba(59,107,59,0.15); }
.dm-eng { margin: 4px 0 0; font-size: 0.75rem; color: #8a9e7f; font-style: italic; }
.dm-symptom { margin: 8px 0 0; font-size: 0.8rem; color: #5a6d50; line-height: 1.6; }
.dm-cause { margin: 6px 0 0; font-size: 0.78rem; color: #6f7d61; line-height: 1.65; }
.dm-ctrl { margin: 6px 0 0; font-size: 0.8rem; color: #3B6B3B; }
.dm-phi { display: inline-block; margin-top: 8px; font-size: 0.72rem; background: #fff8e1; color: #e65100; padding: 3px 10px; border-radius: 4px; font-weight: 600; }
.dm-normal { display: flex; flex-direction: column; align-items: center; gap: 10px; padding: 30px 0; color: #7a9670; font-size: 0.9rem; }
.dm-right { display: flex; flex-direction: column; gap: 12px; }
.dm-right h4 { margin: 0; font-size: 1rem; color: #2d5a27; }
.dm-mix { display: flex; gap: 16px; align-items: flex-start; }
.dm-cyl { position: relative; width: 56px; height: 170px; border-radius: 28px; border: 4px solid rgba(59,107,59,0.15); overflow: hidden; flex-shrink: 0; transition: border-color 0.5s; }
.cyl-animating { border-color: rgba(66,165,245,0.45); box-shadow: 0 0 12px rgba(66,165,245,0.15); }
.dm-cyl-inner { display: flex; flex-direction: column-reverse; height: 100%; }
.dm-blk { flex: 1; min-height: 0; transition: background 0.12s; border-radius: 1px; }
.dm-cyl-label { position: absolute; top: 4px; left: 50%; transform: translateX(-50%); font-size: 0.55rem; background: rgba(0,0,0,0.5); color: #fff; padding: 1px 6px; border-radius: 8px; white-space: nowrap; }
.dm-cyl-label.locked { background: rgba(198,40,40,0.85); color: #fff; font-size: 0.6rem; padding: 2px 8px; animation: lockPulse 1.5s infinite; }
.dm-cyl-label.empty-bottle { top: 50%; left: 50%; transform: translate(-50%, -50%); background: transparent; padding: 0; animation: none; display: flex; align-items: center; justify-content: center; }
@keyframes lockPulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.5; } }
.dm-stack-wrap { flex: 1; display: flex; flex-direction: column; justify-content: center; gap: 6px; }
.dm-stack-label { font-size: 0.72rem; color: #7a9670; }
.dm-stack-bar { display: flex; height: 26px; border-radius: 10px; overflow: hidden; }
.dm-stack-bar > div { transition: width 0.8s ease; }
.dm-pumps { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; }
.dm-pumps.pumps-two { grid-template-columns: repeat(2, 1fr); }
.dm-pump { text-align: center; padding: 12px 8px; border-radius: 10px; background: rgba(59,107,59,0.04); }
.dm-pump span { display: block; font-size: 0.7rem; color: #7a9670; }
.dm-pump strong { font-size: 1.15rem; color: #3B6B3B; }
.dm-ai-text { margin: 4px 0 0; font-size: 0.8rem; color: #5a6d50; line-height: 1.6; }
.dm-ai-mechanism { margin: 6px 0 0; font-size: 0.78rem; color: #6e7f63; line-height: 1.7; }

/* 知识库弹窗 */
.pest-info-overlay { position: fixed; inset: 0; background: rgba(0,0,0,0.4); z-index: 100; display: flex; align-items: center; justify-content: center; backdrop-filter: blur(4px); }
.pest-info-card { background: #fff; border-radius: 16px; max-width: 420px; width: 90%; box-shadow: 0 12px 40px rgba(0,0,0,0.2); position: relative; overflow: hidden; }
.pest-info-close { position: absolute; top: 12px; right: 12px; z-index: 2; border: none; background: rgba(0,0,0,0.1); border-radius: 50%; width: 32px; height: 32px; display: flex; align-items: center; justify-content: center; color: #555; cursor: pointer; }
.pest-info-body { padding: 20px; }
.pest-info-img { width: 100%; height: 160px; object-fit: cover; border-radius: 10px; margin-bottom: 14px; border: 1px solid #eee; }
.pest-info-body h4 { margin: 0; font-size: 1.2rem; color: #2d5a27; }
.pest-info-eng { margin: 4px 0 0; font-size: 0.8rem; color: #8a9e7f; font-style: italic; }
.pest-info-cat { display: inline-block; margin-top: 8px; font-size: 0.72rem; background: rgba(59,107,59,0.1); color: #3B6B3B; padding: 3px 10px; border-radius: 4px; }
.pest-info-sections { margin-top: 14px; display: flex; flex-direction: column; gap: 10px; }
.pest-info-sections strong { font-size: 0.82rem; color: #3B6B3B; }
.pest-info-sections p { margin: 4px 0 0; font-size: 0.8rem; color: #5a6d50; line-height: 1.6; }
.pest-info-phi { margin-top: 14px; padding: 10px; background: linear-gradient(135deg, #fff8e1, #ffecb3); border-radius: 8px; text-align: center; font-size: 0.82rem; font-weight: 600; color: #e65100; }

/* 底部状态栏 — 始终固定在页面底部 */
.monitor-footer { margin-top: auto; flex-shrink: 0; display: flex; justify-content: space-between; align-items: center; background: rgba(255,255,255,0.6); border: 1px solid rgba(59,107,59,0.08); border-radius: 10px; padding: 8px 14px; font-size: 0.82rem; color: #6b8a65; }
.footer-stats { display: flex; gap: 16px; }
.footer-stats strong { color: #2d5a27; }
.footer-stats .red { color: #c62828; }
.footer-live { display: flex; align-items: center; gap: 4px; color: #3B6B3B; font-weight: 600; }
.live-dot { width: 7px; height: 7px; border-radius: 50%; background: #4caf50; animation: ping 2s infinite; }
</style>
