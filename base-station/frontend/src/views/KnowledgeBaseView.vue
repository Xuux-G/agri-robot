<script setup>
import { ref, computed } from "vue";
import AIAdvisor from "../components/AIAdvisor.vue";

// 病虫害知识库 — 参考 bh.ainyi.cn 的"农作物病虫害库"风格
const pestLibrary = ref([
  {
    id: 1, name: "白斑病", engName: "White Spot (Cercospora)", crop: "菠菜/甜菜", category: "真菌性病害",
    symptom: "下部叶片先发病，病斑圆形至近圆形，边缘明显，大小0.5-3.5mm，中间黄白色，外缘褐至紫褐色，扩展后渐变为白色斑。湿度大时病斑上可见灰色毛状物，干湿变换激烈时病斑中部易破裂。生长势弱、温暖潮湿条件下易发病。",
    control: "收获后及时清除病残体集中深埋；发病初期喷30%绿得保悬浮剂400-500倍液，或75%百菌清可湿性粉剂700倍液，隔7-10天1次连续2-3次。",
    phi: 7, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/7c3f53ad9956cc2f578bca1bb4582478.jpg",
  },
  {
    id: 2, name: "黑斑病", engName: "Alternaria Leaf Spot", crop: "菠菜/白菜", category: "真菌性病害",
    symptom: "主要危害叶片，病斑圆形或近圆形，淡褐色至褐色，直径2-6mm，微具同心轮纹，病斑上生黑色霉状物。病原以菌丝或分生孢子在种子或病残体上越冬，借气流、雨水传播。",
    control: "收获后清除病残体深埋；发病初期喷70%代森锰锌干悬粉500倍液，或75%百菌清可湿性粉剂600倍液，或50%扑海因可湿性粉剂1500倍液。采收前7天停药。",
    phi: 15, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/4f1781f4fc3f5f331d9061074c453552.jpg",
  },
  {
    id: 3, name: "株腐病", engName: "Rhizoctonia Rot / Damping-off", crop: "菠菜/多作物", category: "真菌性病害",
    symptom: "苗期重要病害。刚出土幼苗染病子叶萎蔫，后从根际处倒伏或枯死；大苗染病下部叶片黄化，茎基部产生椭圆形暗褐色病斑，主根呈黑褐色腐烂，植株逐渐干缩枯死。播种过密、间苗不及时、温度过高易诱发。",
    control: "加强苗床管理科学放风，防止高温高湿；用种子重量0.2%的40%拌种双拌种；发病初期喷15%恶霉灵水剂450倍液，或72.2%普力克水剂800倍液。",
    phi: 10, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/121ba379f866cd517e2fc4b51f51e432.jpg",
  },
  {
    id: 4, name: "白粉病", engName: "Powdery Mildew", crop: "多作物", category: "真菌性病害",
    symptom: "叶片表面出现白色粉状霉层，严重时叶片变黄枯死，特征极强。病菌借气流传播，在温暖干燥与高湿交替时易暴发，种植过密通风不良加重发生。",
    control: "吡唑醚菌酯悬浮剂1500倍液喷雾，对白粉病特效。甲氧基丙烯酸酯类杀菌剂，强效抑制真菌孢子萌发和菌丝生长。注意轮换用药避免抗性。",
    pesticide: "吡唑醚菌酯", phi: 7, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/3d1401288196ada1be792b8d9ea968be.jpg",
  },
  {
    id: 5, name: "褐斑病", engName: "Brown Spot (Cercospora)", crop: "多作物", category: "真菌性病害",
    symptom: "叶片出现褐色圆形或不规则病斑，边缘深褐色中部灰白色，严重时叶片枯黄早落。高温多雨季节发病重，偏施氮肥加重发生。",
    control: "代森锰锌干悬粉500倍液，广谱保护性杀菌剂，在作物表面形成致密药膜阻止病菌侵入，持效期长且不易产生抗药性。重点喷洒叶片正反面。",
    pesticide: "代森锰锌", phi: 15, image: "/uploads/heban_demo.jpg",
  },
  {
    id: 6, name: "灰霉病", engName: "Gray Mold (Botrytis cinerea)", crop: "番茄/草莓/辣椒/菠菜", category: "真菌性病害",
    symptom: "花、果实、叶片均可受害，产生灰色霉层，果实变软腐烂。低温高湿时暴发，保护地栽培尤为严重。病菌借气流、雨水传播，从伤口或衰败组织侵入。",
    control: "嘧霉胺悬浮剂1000倍液+通风降湿；及时摘除病果病叶集中处理；大棚注意控湿，晴天上午浇水。",
    phi: 7, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/34179a5ddfd9cf6613206f92186728dd.jpg",
  },
  {
    id: 7, name: "霜霉病", engName: "Downy Mildew", crop: "黄瓜/菠菜/生菜/白菜", category: "真菌性病害",
    symptom: "叶面出现淡黄色多角形病斑，受叶脉限制；叶背产生白色霜状霉层，严重时叶片枯黄。高湿是发病主要条件，多雨多雾季节易流行。",
    control: "霜脲·锰锌可湿性粉剂600倍液，或烯酰吗啉悬浮剂1500倍液。发病前预防性喷施效果最佳，重点喷叶背。",
    phi: 10, image: "http://bh.ainyi.cn/static/upload/image/2025-02-18/48fb66642253a228911cc3b328f7a585.jpg",
  },
  {
    id: 8, name: "枯萎病", engName: "Fusarium Wilt", crop: "菠菜/草莓/番茄", category: "真菌性病害",
    symptom: "植株3片真叶后开始发病，初期叶片变暗失去光泽，逐渐萎蔫黄化；根部变褐枯死，发病早的植株明显矮化。高温干燥天气病株迅速死亡。土温25-30℃且土壤潮湿易发病。",
    control: "与葱蒜类或禾本科作物3-5年轮作；发病初期喷40%多·硫悬浮剂500倍液，或60%琥·乙磷铝可湿性粉剂400倍液，隔10天1次连防2-3次。",
    phi: 14, image: "http://bh.ainyi.cn/static/upload/image/2025-02-18/bfc5e07d1b95bfa8916eb79f296dd292.jpg",
  },
  {
    id: 9, name: "炭疽病", engName: "Anthracnose", crop: "菠菜/辣椒/草莓", category: "真菌性病害",
    symptom: "叶片出现圆形或不规则褐色病斑，边缘深褐色，后期病斑中央灰白色易穿孔。茎部染病产生梭形凹陷斑。高湿多雨季节发病重，露地栽培发病较重。",
    control: "发病初期喷25%咪鲜胺乳油1000倍液，或10%苯醚甲环唑水分散粒剂1500倍液，隔7-10天1次连续2-3次。",
    phi: 10, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/683a6dc67687d4dd9b6cb04a8d36bf97.jpg",
  },
  {
    id: 10, name: "叶斑病", engName: "Phyllosticta Leaf Spot", crop: "菠菜/多作物", category: "真菌性病害",
    symptom: "叶片出现近圆形或不规则褐色至灰白色病斑，边缘深褐色，病斑可融合成不规则大斑。病菌随病残体在土壤中越冬，借风雨传播。",
    control: "清除田间病残体；发病初期喷50%多菌灵可湿性粉剂800倍液，或70%甲基硫菌灵可湿性粉剂1000倍液。",
    phi: 10, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/c1ecc5e9958f3e2c47b987231ea9ff36.jpg",
  },
  {
    id: 11, name: "低温障碍", engName: "Cold Injury", crop: "菠菜/多作物", category: "生理性病害",
    symptom: "越冬或早春栽培受到低温或寒流侵袭时受冻，轻者叶缘变白呈薄纸状，严重者似开水烫过。持续低于-5℃或日均温降至-8℃以下会严重冻害。",
    control: "选用耐低温品种；安排在背风向阳处；施用腐熟有机肥控氮增磷钾肥增强抗寒力；寒流前灌水调温、适时中耕疏松土壤提高地温。",
    phi: 0, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/9601f301f7005bbe514f2635632c432a.jpg",
  },
  {
    id: 12, name: "蚜虫", engName: "Aphid (Myzus persicae)", crop: "多作物", category: "刺吸式害虫",
    symptom: "成虫和若蚜群集嫩叶嫩芽背面刺吸汁液，致叶片向背面卷曲皱缩变形，分泌蜜露诱发煤污病。年发生10-40代世代重叠严重，以春秋两季为发生高峰。还可传播多种植物病毒病。",
    control: "吡虫啉可湿性粉剂2000倍液，或啶虫脒乳油2000倍液，或吡蚜酮可湿性粉剂3000倍液。利用黄板诱杀有翅蚜；保护瓢虫、草蛉等天敌。",
    phi: 7, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/98c707ad11f6e0f4a212d7699c6c50ab.jpg",
  },
  {
    id: 13, name: "桃蚜", engName: "Green Peach Aphid (Myzus persicae)", crop: "菠菜/辣椒/番茄/桃树", category: "刺吸式害虫",
    symptom: "为害352种寄主。成蚜和若蚜群集嫩叶背面吸食，致叶片卷曲皱缩，传播黄瓜花叶病毒等多种病毒病。有翅蚜对黄色有强烈趋性，对银灰色有负趋性。年发生10-40代。",
    control: "25%天王星乳油2000倍液，或10%吡虫啉可湿性粉剂1000-2000倍液。银灰色地膜覆盖驱避；黄板诱杀；保护食蚜蝇、瓢虫等天敌。",
    phi: 7, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/98c707ad11f6e0f4a212d7699c6c50ab.jpg",
  },
  {
    id: 14, name: "蓟马", engName: "Thrips", crop: "辣椒/茄子/黄瓜/菠菜", category: "刺吸式害虫",
    symptom: "成虫和若虫锉吸植株幼嫩组织汁液，嫩叶嫩梢变硬卷曲枯萎，果实受害后出现锈褐色疤痕。体型极小（1-2mm），隐蔽性强，干旱季节发生重。",
    control: "乙基多杀菌素悬浮剂1000倍液，或啶虫脒乳油1500倍液。清晨或傍晚施药，重点喷洒花器和嫩叶。蓝板诱杀成虫。",
    phi: 5, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/b6f23ac37a9e9500335c4183b239cfef.jpg",
  },
  {
    id: 15, name: "大青叶蝉", engName: "Green Leafhopper (Tettigella viridis)", crop: "多作物", category: "刺吸式害虫",
    symptom: "成虫体长7-10mm青绿色，若虫和成虫群集叶背刺吸汁液，致叶片褪色、畸形、卷缩甚至全叶枯死，还可传播病毒病。年发生3代，以卵在树枝皮内越冬。成虫趋光性极强。",
    control: "防除田间杂草；黑光灯诱杀成虫；发生量大时喷20%甲氰菊酯乳油1500-2000倍液或10%吡虫啉可湿性粉剂3000倍液，隔10天1次连喷2-3次。",
    phi: 7, image: "http://bh.ainyi.cn/static/upload/2025/02/22/202502222468.jpg",
  },
  {
    id: 16, name: "甜菜夜蛾", engName: "Beet Armyworm", crop: "菠菜/白菜/番茄/辣椒", category: "咀嚼式害虫",
    symptom: "初孵幼虫结疏松网在叶背群集取食叶肉，受害部位呈网状半透明的窗斑，干枯后纵裂。3龄后分散为害将叶片吃成孔洞缺刻，严重时全部叶片被食尽。4龄后昼伏夜出，有假死性。幼虫表皮光滑药液不易粘附，3龄后抗药性强。",
    control: "清晨或傍晚施药最佳。5%夜蛾必杀乳油1000-2000倍液+菊酯伴侣500倍混合液，或甲维盐乳油2000倍液。人工摘除卵块、捕捉幼虫可有效降低虫口密度。",
    phi: 10, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/f12ebe178bfb0306522ff0d33d95ff1e.jpg",
  },
  {
    id: 17, name: "菜螟", engName: "Cabbage Webworm (Hellula undalis)", crop: "菠菜/白菜/萝卜/甘蓝", category: "咀嚼式害虫",
    symptom: "幼虫是钻蛀性害虫。初孵幼虫潜叶为害隧道宽短；2龄后穿出叶面；3龄吐丝缀合心叶在内取食使心叶枯死；4-5龄可由心叶或叶柄蛀入茎髓或根部，蛀孔外缀有细丝和潮湿虫粪，引起腐烂。幼虫有转株危害习性，1头可转株4-5株。",
    control: "耕翻土地杀灭越冬幼虫；幼虫孵化盛期喷48%毒死蜱乳油1000-1500倍液，或5%抑太保乳油2000-3000倍液，重点喷心叶部位。调整播种期错开菜螟盛发期。",
    phi: 7, image: "http://bh.ainyi.cn/static/upload/2025/02/22/202502227885.jpg",
  },
  {
    id: 18, name: "油葫芦", engName: "Field Cricket (Teleotryllus emma)", crop: "多作物", category: "地下害虫",
    symptom: "成虫体长22-25mm黑褐色。成虫和若虫昼间隐蔽夜间活动，咬食幼苗茎叶成缺刻或孔洞，有的咬食花荚或根。每年发生1代，以卵在土中越冬。成虫有趋光性。",
    control: "利用成虫趋光性黑光灯诱杀；苗期每亩用50%辛硫磷乳油40-60ml拌炒香麦麸450-600kg撒施田间；或50%辛硫磷乳油50-60ml拌细土1000kg撒入田中。从田四周向中间推进效果好。",
    phi: 0, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/7cd5ea5314f7efc5bf80f0f0595a5ba3.jpg",
  },
  {
    id: 19, name: "红蜘蛛", engName: "Red Spider Mite (Tetranychus)", crop: "茄子/辣椒/豆类/草莓", category: "刺吸式害虫",
    symptom: "成螨和若螨群集叶背刺吸汁液，受害叶片出现黄白色小斑点，严重时叶片枯焦变红脱落。高温干燥季节易暴发，年发生10-20代，世代重叠严重。",
    control: "阿维菌素乳油2000倍液+哒螨灵可湿性粉剂1500倍液；或联苯肼酯悬浮剂2000倍液。重点喷叶背，隔5-7天1次连喷2-3次。注意轮换用药避免抗性。",
    phi: 7, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/b6f23ac37a9e9500335c4183b239cfef.jpg",
  },
  {
    id: 20, name: "根腐病", engName: "Root Rot (Pythium / Phytophthora)", crop: "菠菜/草莓/辣椒", category: "真菌性病害",
    symptom: "根部变褐腐烂，地上部叶片发黄萎蔫，严重时整株枯死。土壤潮湿、排水不良、重茬连作地块发病重。病原以卵孢子在土壤中越冬，借灌溉水传播。",
    control: "高畦栽培改善排水；与禾本科作物3年以上轮作；发病初期用58%甲霜·锰锌可湿性粉剂500倍液灌根，每株250ml，隔7-10天1次连续2-3次。",
    phi: 14, image: "http://bh.ainyi.cn/static/upload/image/2025-02-18/b5e774b3247a92567c480299dc9d7479.jpg",
  },
  {
    id: 21, name: "潜叶蝇", engName: "Leaf Miner (Pegomya)", crop: "菠菜/萝卜/白菜", category: "咀嚼式害虫",
    symptom: "幼虫潜在叶内取食叶肉，仅留上下表皮，呈块状或弯曲隧道，严重时叶片枯死。成虫为小蝇类，产卵于叶背。春秋两季发生重。",
    control: "清除田间杂草和被害叶片；成虫盛发期喷1.8%阿维菌素乳油2000倍液，或75%灭蝇胺可湿性粉剂3000倍液。",
    phi: 7, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/80c1a1e4cc70547b7a6fdcec59be2bf7.jpg",
  },
  {
    id: 22, name: "小地老虎", engName: "Black Cutworm (Agrotis)", crop: "多作物", category: "地下害虫",
    symptom: "幼虫咬断幼苗茎基部造成缺苗断垄。老熟幼虫体长37-47mm黄褐色至黑褐色，昼伏夜出，3龄后食量大增，有假死性和互相残杀习性。长江流域年发生4-5代。",
    control: "清除田间杂草减少产卵场所；糖醋液或黑光灯诱杀成虫；1-3龄幼虫期喷48%毒死蜱乳油1000倍液或2.5%高效氯氟氰菊酯乳油2000倍液；毒饵诱杀高龄幼虫。",
    phi: 14, image: "http://bh.ainyi.cn/static/upload/image/2025-02-18/11b4a1eb84a96ac687758735e29e9af9.jpg",
  },
  {
    id: 23, name: "斜纹夜蛾", engName: "Tobacco Cutworm (Spodoptera litura)", crop: "多作物", category: "咀嚼式害虫",
    symptom: "一种间隙暴发为害的杂食性害虫。初孵幼虫群集叶背取食叶肉留上表皮呈窗纱状；2龄后分散为害将叶片吃成孔洞缺刻；大发生时可将全田植株叶片吃光仅剩叶脉。成虫有强趋光性和趋化性。",
    control: "黑光灯或糖醋液诱杀成虫；人工摘除卵块和初孵幼虫群集叶片；低龄幼虫期喷5%甲维盐乳油2000倍液或20%虫酰肼悬浮剂1500倍液。",
    phi: 10, image: "http://bh.ainyi.cn/static/upload/2025/02/21/250221081158633.jpg",
  },
  {
    id: 24, name: "蝼蛄", engName: "Mole Cricket (Gryllotalpa)", crop: "多作物", category: "地下害虫",
    symptom: "成虫和若虫在土中活动，咬食种子和幼苗根系，在表土层挖掘隧道使幼苗根部与土壤分离失水枯死。东方蝼蛄体长30-35mm黄褐色，昼伏夜出，有趋光性和趋粪肥性。",
    control: "马粪或灯光诱杀；毒饵诱杀：50%辛硫磷乳油拌炒香麦麸撒施；土壤处理：每亩用5%辛硫磷颗粒剂2-3kg拌细土20kg沟施或撒施后浅锄。",
    phi: 14, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/c150e01d1a33efad73c964a92c62fee8.jpg",
  },
  {
    id: 25, name: "茎枯病", engName: "Stem Blight (Phoma)", crop: "菠菜/番茄/辣椒", category: "真菌性病害",
    symptom: "主要危害茎部，发病初期在茎上形成大小不等的梭形或不规则灰色病斑，边缘黑褐色，逐渐扩大绕茎一周后上部枝叶枯死。高湿多雨季节发病重。",
    control: "合理密植改善通风透光；发病初期喷50%多菌灵可湿性粉剂800倍液，或70%甲基硫菌灵可湿性粉剂1000倍液，重点喷洒茎基部。",
    phi: 10, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/08ccbf1c4964b83fa43641a3ac1efc5f.jpg",
  },
  {
    id: 26, name: "菜青虫", engName: "Cabbage Worm (Pieris rapae)", crop: "辣椒/白菜/甘蓝/花椰菜", category: "咀嚼式害虫",
    symptom: "幼虫啃食叶片形成孔洞和缺刻，严重时仅剩叶脉和叶柄。幼虫排出大量粪便污染菜心，影响蔬菜商品价值。成虫为白色粉蝶，产卵于叶背。",
    control: "推荐优先用1.8%阿维菌素乳油稀释1500-2000倍喷雾，其为大环内酯类生物农药，可干扰害虫神经传导使其麻痹死亡；但需结合虫龄和当地抗药性灵活调浓度，低龄幼虫期效果最好。若田间抗性偏高，可轮换20%氯虫苯甲酰胺悬浮剂亩用10ml，或5%甲维盐悬浮剂3000倍液，提高防效并延缓抗性。",
    pesticide: "阿维菌素", phi: 7, image: "/uploads/caiqingchong.jpg",
  },
  {
    id: 27, name: "蜗牛", engName: "Snail / Slug", crop: "猕猴桃/蔬菜/草莓/花卉", category: "软体动物害",
    symptom: "舔食叶片形成不规则孔洞，爬行处留下银白色黏液痕迹，嫩叶嫩果受害最重。阴雨潮湿环境利于蜗牛活动，果园生草覆盖、排水不畅时密度激增。",
    control: "四聚乙醛颗粒剂撒施或喷雾，专用杀螺剂，对蜗牛和蛞蝓有强烈引诱和触杀作用，使螺体大量分泌黏液脱水死亡。傍晚施药效果最佳。",
    pesticide: "四聚乙醛", phi: 7, image: "/uploads/woniu.jpg",
  },
  {
    id: 28, name: "病毒病", engName: "Virus Disease (CMV/TMV)", crop: "辣椒/番茄/黄瓜/烟草", category: "病毒性病害",
    symptom: "叶片出现黄绿相间的花叶、斑驳或皱缩畸形，植株矮化生长迟缓，果实出现坏死条纹或畸形。由蚜虫等刺吸式害虫传播，高温干旱年份易流行。",
    control: "以预防为主：选用抗病品种，及时防治蚜虫切断传播途径。发病初期喷施盐酸吗啉胍+芸苔素内酯，增强植株免疫力。严重病株及时拔除。",
    pesticide: "芸苔素内酯（辅助）", phi: 0, image: "http://bh.ainyi.cn/static/upload/image/2025-02-21/9601f301f7005bbe514f2635632c432a.jpg",
  },
]);

const searchQuery = ref("");
const activeCategory = ref("all");

const categories = computed(() => {
  const cats = new Set(pestLibrary.value.map((p) => p.category));
  return ["all", ...cats];
});

const filteredPests = computed(() => {
  let list = pestLibrary.value;
  if (activeCategory.value !== "all") {
    list = list.filter((p) => p.category === activeCategory.value);
  }
  const q = searchQuery.value.trim().toLowerCase();
  if (q) {
    list = list.filter(
      (p) =>
        p.name.toLowerCase().includes(q) ||
        p.crop.toLowerCase().includes(q) ||
        p.engName.toLowerCase().includes(q) ||
        p.category.toLowerCase().includes(q)
    );
  }
  return list;
});

const selectedPest = ref(null);
const lightboxSrc = ref("");

function openDetail(pest) {
  selectedPest.value = pest;
}

function closeDetail() {
  selectedPest.value = null;
}

function handleViewPest(pestName) {
  const pest = pestLibrary.value.find(p => p.name === pestName);
  if (pest) {
    selectedPest.value = pest;
    setTimeout(() => {
      const el = document.querySelector('.detail-card');
      if (el) el.scrollIntoView({ behavior: 'smooth', block: 'start' });
    }, 100);
  }
}
</script>

<template>
  <div class="kb-page" :class="{ 'detail-open': !!selectedPest }">
    <!-- 一级界面：顶部 + 筛选 + 搜索 + 卡片网格 -->
    <template v-if="!selectedPest">
      <div class="kb-hero">
        <div class="kb-hero-text">
          <h2>植保知识库</h2>
          <p>收录蔬菜作物常见病虫害的识别特征、防治方法与安全间隔期</p>
        </div>
        <div class="kb-hero-stats">
          <div class="stat-item">
            <span class="stat-num">28</span>
            <span class="stat-label">病虫害条目</span>
          </div>
          <div class="stat-item">
            <span class="stat-num">6</span>
            <span class="stat-label">分类类别</span>
          </div>
        </div>
      </div>

      <div class="kb-toolbar">
        <div class="category-filters">
          <button
            v-for="cat in categories"
            :key="cat"
            :class="['cat-btn', { active: activeCategory === cat }]"
            @click="activeCategory = cat"
          >
            {{ cat === "all" ? "全部类别" : cat }}
          </button>
        </div>
        <div class="search-box">
          <svg class="search-icon" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><path d="M21 21l-4.35-4.35"/></svg>
          <input v-model="searchQuery" type="text" placeholder="搜索病虫害名称、作物、学名..." />
        </div>
      </div>

      <div class="kb-grid">
        <article v-for="pest in filteredPests" :key="pest.id" class="kb-card" @click="openDetail(pest)">
          <div class="kb-card-img">
            <img v-if="pest.image" :src="pest.image" :alt="pest.name" class="pest-thumb" @error="$event.target.style.display='none'" />
            <div v-if="!pest.image" class="img-placeholder" :class="'cat-' + pest.category">
              <svg width="48" height="48" viewBox="0 0 48 48" fill="none">
                <circle cx="24" cy="24" r="20" stroke="currentColor" stroke-width="1.5" opacity="0.3"/>
                <path d="M20 30c3-1 8-5 8-10s-4-8-12-6-12 10-8 16 24 4 28-4" stroke="currentColor" stroke-width="2" fill="none"/>
              </svg>
            </div>
            <span class="crop-tag">{{ pest.crop }}</span>
          </div>
          <div class="kb-card-body">
            <h3>{{ pest.name }}</h3>
            <p class="eng-name">{{ pest.engName }}</p>
            <div class="card-meta">
              <span class="cat-badge">{{ pest.category }}</span>
              <span v-if="pest.phi > 0" class="phi-badge">PHI {{ pest.phi }}天</span>
            </div>
          </div>
        </article>
        <div v-if="filteredPests.length === 0" class="empty-state">未找到匹配的病虫害条目</div>
      </div>
    </template>

    <!-- 二级界面：沉浸式详情 -->
    <div v-else class="kb-detail-full">
      <button class="back-btn" @click="closeDetail">
        <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5m7-7l-7 7 7 7"/></svg>
        返回列表
      </button>

      <div class="detail-body">
        <!-- 左侧：标题 + 文字内容 -->
        <div class="detail-main">
          <div class="detail-header">
            <h2>{{ selectedPest.name }}</h2>
            <p class="detail-eng">{{ selectedPest.engName }}</p>
            <div class="detail-tags">
              <span>{{ selectedPest.crop }}</span>
              <span>{{ selectedPest.category }}</span>
            </div>
          </div>

          <section class="detail-section">
            <h4>为害症状</h4>
            <p>{{ selectedPest.symptom }}</p>
          </section>

          <section class="detail-section">
            <h4>防治方法</h4>
            <p>{{ selectedPest.control }}</p>
          </section>
        </div>

        <!-- 右侧：图片 + PHI 堆叠，图片顶部与标题对齐 -->
        <div class="detail-side">
          <img
            v-if="selectedPest.image"
            :src="selectedPest.image"
            :alt="selectedPest.name"
            class="detail-img"
            @click="lightboxSrc = selectedPest.image"
            @error="$event.target.style.display='none'"
          />
          <div v-else class="detail-img-fallback" :class="'cat-' + selectedPest.category">
            <svg width="40" height="40" viewBox="0 0 48 48" fill="none">
              <circle cx="24" cy="24" r="20" stroke="currentColor" stroke-width="1.5" opacity="0.3"/>
              <path d="M20 30c3-1 8-5 8-10s-4-8-12-6-12 10-8 16 24 4 28-4" stroke="currentColor" stroke-width="2" fill="none"/>
            </svg>
          </div>

          <div class="phi-card" v-if="selectedPest.phi > 0">
            <div class="phi-value">{{ selectedPest.phi }}</div>
            <div class="phi-unit">天</div>
            <p>安全间隔期 (PHI)</p>
          </div>
        </div>
      </div>

      <!-- 配伍禁忌 - 全宽 -->
      <div class="info-note-full">
        <h4>配伍禁忌提示</h4>
        <p>施药前检查与上次用药是否属同一作用机理类型，避免连续使用同类药剂导致抗药性。</p>
      </div>
    </div>

    <!-- AI 植保顾问 - 一级页面底部；详情页时仅隐藏，保留咨询结果 -->
    <div v-show="!selectedPest">
      <AIAdvisor @viewPest="handleViewPest" />
    </div>

    <!-- 图片灯箱 -->
    <div v-if="lightboxSrc" class="kb-lightbox" @click.self="lightboxSrc=''">
      <button class="kb-lightbox-close" @click="lightboxSrc=''">
        <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
      </button>
      <img :src="lightboxSrc" class="kb-lightbox-img" />
    </div>
  </div>
</template>

<style scoped>
.kb-page {
  height: 100%;
  display: flex;
  flex-direction: column;
  gap: 20px;
  padding: 24px;
  overflow: auto;
  background:
    /* 噪点纹理 */
    url("data:image/svg+xml,%3Csvg viewBox='0 0 200 200' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.7' numOctaves='3' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)' opacity='0.025'/%3E%3C/svg%3E"),
    /* 稻田色带 */
    linear-gradient(180deg,
      #f5efe0 0%,
      #e8f0e0 25%,
      #d4e8c8 45%,
      #c5d8b0 60%,
      #b8c8a0 78%,
      #9aaf82 92%,
      #8a9e78 100%
    );
  border-radius: 16px;
  border: 1px solid rgba(59, 107, 59, 0.06);
}

/* 顶部 -- 左对齐，不对称 */
.kb-hero {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 24px;
  padding-bottom: 20px;
  border-bottom: 1px solid rgba(59, 107, 59, 0.1);
}

.kb-hero-text h2 {
  margin: 0;
  font-size: 1.6rem;
  color: #2d5a27;
  font-weight: 700;
}

.kb-hero-text p {
  margin: 8px 0 0;
  color: #6b8a65;
  font-size: 0.88rem;
}

.kb-hero-stats {
  display: flex;
  gap: 32px;
}

.stat-item {
  display: flex;
  flex-direction: column;
  align-items: flex-end;
}

.stat-num {
  font-size: 2rem;
  font-weight: 800;
  color: #3B6B3B;
  line-height: 1;
}

.stat-label {
  font-size: 0.75rem;
  color: #8a9e7f;
  margin-top: 4px;
}

/* 工具栏 */
.kb-toolbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 16px;
  flex-wrap: wrap;
}

.category-filters {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}

.cat-btn {
  border: 1px solid rgba(59, 107, 59, 0.2);
  background: rgba(255,255,255,0.6);
  color: #5a7d4a;
  padding: 6px 14px;
  border-radius: 6px;
  font-size: 0.8rem;
  cursor: pointer;
  transition: all 0.2s;
}

.cat-btn:hover {
  background: rgba(59, 107, 59, 0.08);
}

.cat-btn.active {
  background: #3B6B3B;
  color: #fff;
  border-color: #3B6B3B;
}

.search-box {
  display: flex;
  align-items: center;
  gap: 8px;
  background: rgba(255,255,255,0.7);
  border: 1px solid rgba(59, 107, 59, 0.15);
  border-radius: 8px;
  padding: 8px 14px;
  min-width: 240px;
}

.search-icon {
  color: #8a9e7f;
  flex-shrink: 0;
}

.search-box input {
  border: none;
  background: transparent;
  outline: none;
  font-size: 0.85rem;
  color: #3a5530;
  width: 100%;
}

.search-box input::placeholder {
  color: #b0c4a5;
}

/* 卡片网格 */
.kb-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
  gap: 14px;
}

.kb-card {
  background: rgba(255,255,255,0.75);
  border: 1px solid rgba(59, 107, 59, 0.08);
  border-radius: 12px;
  overflow: hidden;
  cursor: pointer;
  transition: all 0.25s;
  box-shadow: 0 2px 12px rgba(30, 60, 30, 0.04);
}

.kb-card:hover {
  transform: translateY(-2px);
  box-shadow: 0 6px 20px rgba(30, 60, 30, 0.08);
  border-color: rgba(59, 107, 59, 0.2);
}

.kb-card-img {
  height: 120px;
  display: flex;
  align-items: center;
  justify-content: center;
  position: relative;
  overflow: hidden;
}

.pest-thumb {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}

.img-placeholder {
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
}

.img-placeholder.cat-真菌性病害 { background: linear-gradient(135deg, #e8f5e9, #c8e6c9); color: #2e7d32; }
.img-placeholder.cat-刺吸式害虫 { background: linear-gradient(135deg, #fff3e0, #ffe0b2); color: #e65100; }
.img-placeholder.cat-咀嚼式害虫 { background: linear-gradient(135deg, #fce4ec, #f8bbd0); color: #c62828; }
.img-placeholder.cat-地下害虫 { background: linear-gradient(135deg, #ede7f6, #d1c4e9); color: #4527a0; }
.img-placeholder.cat-生理性病害 { background: linear-gradient(135deg, #e3f2fd, #bbdefb); color: #1565c0; }

.crop-tag {
  position: absolute;
  top: 8px;
  right: 8px;
  background: rgba(0,0,0,0.55);
  color: #fff;
  font-size: 0.7rem;
  padding: 3px 8px;
  border-radius: 4px;
}

.kb-card-body {
  padding: 14px;
}

.kb-card-body h3 {
  margin: 0;
  font-size: 0.95rem;
  color: #2d4a22;
}

.eng-name {
  margin: 4px 0 0;
  font-size: 0.72rem;
  color: #8a9e7f;
  font-style: italic;
}

.card-meta {
  display: flex;
  gap: 6px;
  margin-top: 10px;
}

.cat-badge {
  font-size: 0.7rem;
  background: rgba(59, 107, 59, 0.1);
  color: #3B6B3B;
  padding: 2px 8px;
  border-radius: 4px;
}

.phi-badge {
  font-size: 0.7rem;
  background: rgba(255, 152, 0, 0.12);
  color: #e65100;
  padding: 2px 8px;
  border-radius: 4px;
  font-weight: 600;
}

.card-pesticide {
  font-size: 0.72rem;
  color: #2e7d32;
  margin-top: 6px;
  font-weight: 500;
}

.empty-state {
  grid-column: 1 / -1;
  text-align: left;
  padding: 40px;
  color: #8a9e7f;
  font-size: 0.9rem;
}

/* 二级详情 - 沉浸式 */
.kb-page.detail-open { gap: 0; }

.kb-detail-full {
  flex: 1; display: flex; flex-direction: column; gap: 14px; min-height: 0;
}

.back-btn {
  display: flex; align-items: center; gap: 6px;
  border: none; background: transparent; color: #3B6B3B; font-size: 0.9rem;
  cursor: pointer; padding: 0; width: fit-content; flex-shrink: 0;
}
.back-btn:hover { opacity: 0.7; }

.detail-header h2 {
  margin: 0; font-size: 1.45rem; color: #2d5a27;
}
.detail-eng { margin: 4px 0 0; color: #8a9e7f; font-style: italic; }
.detail-tags { display: flex; gap: 8px; margin-top: 8px; }
.detail-tags span {
  background: rgba(59,107,59,0.08); color: #3B6B3B; font-size: 0.78rem; padding: 3px 10px; border-radius: 6px;
}

/* 内容主体：左(标题+文字) + 右(图+PHI堆叠) */
.detail-body {
  display: grid; grid-template-columns: 1fr 220px; gap: 24px; flex: 1; min-height: 0;
  align-items: start;
}

/* 左侧标题区 */
.detail-header h2 {
  margin: 0; font-size: 1.5rem; color: #2d5a27;
}
.detail-eng { margin: 6px 0 0; color: #8a9e7f; font-style: italic; font-size: 0.88rem; }
.detail-tags { display: flex; gap: 8px; margin-top: 12px; }
.detail-tags span {
  background: rgba(59,107,59,0.08); color: #3B6B3B; font-size: 0.8rem; padding: 4px 12px; border-radius: 6px;
}

/* 左侧文字区 */
.detail-main { }
.detail-header { margin-bottom: 20px; }
.detail-section { margin-top: 0; }
.detail-section + .detail-section { margin-top: 24px; }
.detail-section h4 { margin: 0 0 10px; font-size: 1rem; color: #2d5a27; font-weight: 600; }
.detail-section p { margin: 0; color: #4a6740; font-size: 0.92rem; line-height: 1.8; }

/* 右侧堆叠：图片顶部与左侧标题对齐，PHI在图片下 */
.detail-side {
  display: flex; flex-direction: column; gap: 12px;
  padding-top: 16px;
}
.detail-img {
  width: 100%; height: auto; max-height: 220px;
  object-fit: cover; border-radius: 12px;
  border: 1px solid rgba(59,107,59,0.1);
  box-shadow: 0 2px 12px rgba(30,60,30,0.05);
  cursor: pointer; transition: transform 0.2s;
}
.detail-img:hover { transform: scale(1.02); }
.detail-img-fallback {
  width: 100%; height: 140px; display: flex; align-items: center; justify-content: center;
  border-radius: 12px; border: 1px solid rgba(59,107,59,0.1);
}

.phi-card {
  background: linear-gradient(135deg, #fff8e1, #ffecb3);
  border: 1px solid rgba(255,152,0,0.2); border-radius: 10px; padding: 16px; text-align: center;
  margin-top: 6px;
}
.phi-value { font-size: 2.4rem; font-weight: 800; color: #e65100; line-height: 1; }
.phi-unit { font-size: 0.88rem; color: #bf360c; margin-top: 2px; }
.phi-card p { margin: 6px 0 0; font-size: 0.75rem; color: #8d6e63; }

/* 配伍禁忌 - 全宽 */
.info-note-full {
  background: rgba(59,107,59,0.06);
  border: 1px solid rgba(59,107,59,0.15);
  border-radius: 10px; padding: 16px 20px;
  margin-top: 20px; flex-shrink: 0;
}
.info-note-full h4 { margin: 0 0 8px; font-size: 0.92rem; color: #2d5a27; font-weight: 600; }
.info-note-full p { margin: 0; font-size: 0.88rem; color: #4a6740; line-height: 1.7; }

/* 图片灯箱 */
.kb-lightbox {
  position: fixed; inset: 0; z-index: 200;
  background: rgba(0,0,0,0.7);
  backdrop-filter: blur(10px);
  display: flex; align-items: center; justify-content: center;
  padding: 40px;
}
.kb-lightbox-close {
  position: absolute; top: 20px; right: 24px;
  border: none; background: rgba(255,255,255,0.15);
  border-radius: 50%; width: 42px; height: 42px;
  display: flex; align-items: center; justify-content: center;
  cursor: pointer; color: #fff; transition: background 0.2s;
}
.kb-lightbox-close:hover { background: rgba(255,255,255,0.3); }
.kb-lightbox-img {
  max-width: 90vw; max-height: 85vh;
  object-fit: contain;
  border-radius: 12px;
  box-shadow: 0 20px 60px rgba(0,0,0,0.5);
}
</style>
