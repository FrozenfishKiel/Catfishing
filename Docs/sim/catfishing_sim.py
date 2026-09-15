# -*- coding: utf-8 -*-
"""
小猫钓鱼 数值模拟 v0.1  (2026-08-16)
依据（已裁定口径）：
- 窝料池：向量叠加、无设计上限、每30s×0.9半衰、Total<1归零；T_base=15s(有窝料)/120s(无)、K=100
  实际咬钩间隔 = T_base / (1 + Total/100)；鱼种构成 = 池内属性占比；Total=0 走基础鱼池
- 遛鱼：猫力(品种固定 50/70/90/130)、猫体力100%、鱼力=体重KG×系数K、鱼体力(鱼表格第一版)
  完美中鱼(1s)：普通鱼 力-20%/体-15%；稀有鱼(供奉>=3) 力-15%/体-10%
  僵持消耗战(每秒)：竿耐久-=鱼力*0.1；鱼体力-=猫力*0.08；猫体力-=鱼力*0.12
  拖鱼(向内游拉线,每秒)：猫体力-=鱼力*0.15；放线恢复 +1.5%/s
  碾压：猫力>=2*鱼力 且 竿强>鱼力 → 直接上岸
  拖下水：鱼力>猫力 且 竿强>猫力 且 强行拉；断竿：竿强<猫力 且 竿强<鱼力
  合力：n 猫力量相加、体力消耗均摊（canon：分摊力量与体力）
- 竿耐久（丙案）：每钓一条 -1 ＋ 僵持按上式；耐久 40/70/120，竿价 100/150 金币
- 抄网线：距离 D<=4m
- 供奉：World_Progress 满 100 结束（融合案下改为第10天校准，此处用于测节奏）

假设（文档未定，标注为 ASSUMPTION，可改参数）：
- 竿强度缺数据：默认令 强度=耐久值(40/70/120)，另扫 60/90/140
- 战斗节奏：鱼向外游 3-6s / 向内游 4-8s 交替；初始距离 10m；线长上限 30m
  拉线收 2.0m/s；歇息漂移 -0.5m/s；放线跑 +1.2m/s；战斗超 600s 判脱钩
- 完美提竿命中率 25%；响应窗内必命中
- 玩家策略：向内游且体力>10 就拉；向外游时若能打僵持(条件D)且体力>50 打僵持，否则放线回体力
- 打窝策略：池 Total<40 且有存货就补一份；随身 5 份，耗尽回营补给 90s
- 鱼价 ASSUMPTION：金币 = max(1, round(体重KG*10))（体重轴打底）
- 基础鱼池（Total=0）：泥鳅/小银鱼/河纹鱼 均等
"""
import random, statistics as st
from collections import Counter

random.seed(42)

# ---------- 数据（鱼表格·第一版） ----------
# name: (wmin, wmax, K, 体力, 供奉, 属性, 水域, 常规刷新)
FISH = {
    '河纹鱼':   (0.4, 3,   1.2, 30,  1, '酵', '河', True),
    '小银鱼':   (0.05,0.4, 0.8, 15,  1, '酵', '河湖', True),
    '小彩鱼':   (0.04,0.4, 1.0, 25,  3, '香', '河湖', True),
    '森林长尾鱼':(3,  8,   2.0, 90,  2, '香', '河湖', True),
    '银月鳟':   (3,  8,   2.1, 100, 3, '香', '河湖', False),  # 特殊时刻限定
    '湖心巨影': (15, 40,  2.8, 260, 10,'腥', '事件', False),  # 多人事件鱼
    '花瓣鱼':   (0.05,0.4, 0.9, 20,  3, '香', '河湖', True),
    '风铃鱼':   (0.05,0.4, 0.9, 20,  3, '香', '河湖', True),
    '咸鱼':     (0.5, 1.2, 1.5, 45,  2, '腥', '河湖', True),
    '臭臭鱼':   (0.2, 0.4, 1.8, 75, -1, '酵', '河湖', True),
    '黑鱼':     (3,  8,   2.3, 100, 2, '腥', '河', True),
    '泥鳅':     (0.05,0.5, 0.7, 15,  1, '酵', '河', True),
    '河口鲈':   (1,  5,   1.8, 80,  2, '腥', '河', True),
}
RARE = {n for n,v in FISH.items() if v[4] >= 3}
BASE_POOL = ['泥鳅','小银鱼','河纹鱼']
CATS = {'普通猫':50, '橘猫':70, '狸花猫':90, '缅因猫':130}
RODS = {1:(40,100), 2:(70,100), 3:(120,150)}  # 耐久, 价格（1级初始赠送记0也行）
GROUNDBAIT = {'虫虫窝':(10,2,1), '花果香窝':(1,10,3), '发酵谷物窝':(0,3,10)}
ATTRS = ['腥','香','酵']

# ---------- 战斗模拟 ----------
def fight(C, S, dur_left, name, weight=None, n_cats=1, perfect=False, verbose=False):
    """返回 (结果, 秒数, 竿磨损)。结果: landed/dragged/rodbreak/escape"""
    wmin,wmax,K,stam,offer,attr,_,_ = FISH[name]
    w = weight if weight is not None else random.uniform(wmin,wmax)
    F = w*K
    if perfect:
        if name in RARE: F*=0.85; stam*=0.90
        else:            F*=0.80; stam*=0.85
    if C >= 2*F and S > F:
        return ('landed', 3, 0, w)
    D, L_MAX = 10.0, 30.0
    catstam, rod_dmg, t = 100.0, 0.0, 0
    outward = True; phase_left = random.uniform(3,6)
    while t < 600:
        t += 1; phase_left -= 1
        if phase_left <= 0:
            outward = not outward
            phase_left = random.uniform(3,6) if outward else random.uniform(4,8)
        if not outward:                       # 鱼向内游（乏）
            if catstam > 10:
                D -= 2.0; catstam -= 0.15*F/n_cats
                if catstam <= 0: return ('dragged', t, rod_dmg, w)
            else:
                D -= 0.5; catstam = min(100, catstam+1.0)
        else:                                 # 鱼向外游（挣扎）
            can_stall = (C > F) and (S > F)
            if can_stall and catstam > 50:
                rod_dmg += 0.1*F; stam -= 0.08*C; catstam -= 0.12*F/n_cats
                if catstam <= 0: return ('dragged', t, rod_dmg, w)
                if dur_left - rod_dmg <= 0: return ('rodbreak', t, rod_dmg, w)
            else:
                D += 1.2; catstam = min(100, catstam+1.5)
                if D > L_MAX:                 # 线放尽，强制拉
                    if F > C and S > C: return ('dragged', t, rod_dmg, w)
                    if S < C and S < F: return ('rodbreak', t, dur_left, w)
                    D -= 1.0; catstam -= 0.15*F/n_cats
                    if catstam <= 0: return ('dragged', t, rod_dmg, w)
        if stam <= 0 or D <= 4.0:
            return ('landed', t, rod_dmg, w)
    return ('escape', t, rod_dmg, w)

# ---------- 模型A：搏斗健康度矩阵 ----------
def model_a():
    print('='*100)
    print('模型A｜搏斗结局矩阵（按最大体重的鱼力；N=300 次战斗蒙特卡洛，普通提竿）')
    print('='*100)
    combos = [('普通猫',1,'普通猫x1'),('狸花猫',1,'狸花猫x1'),('缅因猫',1,'缅因猫x1'),
              ('普通猫',2,'普通猫x2'),('普通猫',3,'普通猫x3'),('缅因猫',2,'缅因猫x2')]
    targets = ['森林长尾鱼','黑鱼','河口鲈','银月鳟','湖心巨影']
    for rod,(dur,price) in RODS.items():
        S = dur   # ASSUMPTION 强度=耐久
        print(f'\n--- {rod}级竿（强度={S} 耐久={dur}） ---')
        head = f'{"阵容":<10}' + ''.join(f'{t:<14}' for t in targets)
        print(head)
        for cat,n,label in combos:
            C = CATS[cat]*n
            row = f'{label:<10}'
            for name in targets:
                wmin,wmax,K,stam,offer,attr,_,_ = FISH[name]
                res = Counter(); times=[]
                for _ in range(300):
                    r,tm,dmg,w = fight(C,S,dur,name,weight=wmax,n_cats=n)
                    res[r]+=1; times.append(tm)
                land = res['landed']/300*100
                tag = f'{land:.0f}%胜'
                worst = max((k for k in res if k!='landed'), key=lambda k:res[k], default='')
                if worst: tag += f'/{ {"dragged":"拖走","rodbreak":"断竿","escape":"脱钩"}[worst]}'
                if land==100 and st.mean(times)<=3.5: tag='碾压'
                row += f'{tag:<14}'
            print(row)
    # 关键单点：消耗战数学（不打距离战时的三方竞速）
    print('\n模型A附：条件D纯僵持竞速（谁先归零；不含距离战）')
    for name in ['黑鱼','银月鳟','湖心巨影']:
        wmin,wmax,K,stam,offer,attr,_,_ = FISH[name]
        F = wmax*K
        for cat,n in [('缅因猫',1),('缅因猫',2),('普通猫',3)]:
            C = CATS[cat]*n
            if C<=F: print(f'  {name}(力{F:.0f}) vs {cat}x{n}(力{C}) → 猫力不足，无法进入僵持'); continue
            t_fish = stam/(0.08*C); t_cat = 100/(0.12*F/n); t_rod3 = 120/(0.1*F)
            first = min(('鱼倒',t_fish),('猫脱力',t_cat),('3级竿断',t_rod3), key=lambda x:x[1])
            print(f'  {name}(力{F:.0f},体{stam}) vs {cat}x{n}: 鱼倒{t_fish:.0f}s 猫脱力{t_cat:.0f}s 竿断{t_rod3:.0f}s → 先发生: {first[0]}')

# ---------- 模型B：一日渔获（河流区，单人/多人共享池） ----------
def interval(total):
    return (15 if total>0 else 120)/(1+total/100)

def pick_fish(pool):
    total = sum(pool)
    if total < 1:
        return random.choice(BASE_POOL)
    r = random.random()*total
    attr = '腥' if r<pool[0] else ('香' if r<pool[0]+pool[1] else '酵')
    cands = [n for n,v in FISH.items() if v[5]==attr and v[7] and '河' in v[6]]
    return random.choice(cands) if cands else random.choice(BASE_POOL)

def day_sim(minutes=20, n_players=1, cat='普通猫', rod=1, bait='发酵谷物窝', runs=120):
    dur0, price = RODS[rod]; S = dur0
    C = CATS[cat]
    agg = Counter(); catch_counts=[]; offer_counts=[]; coin_counts=[]; breaks=0; bait_used=[]
    for _ in range(runs):
        pool=[0.0,0.0,0.0]
        players=[{'state':'idle','timer':0,'dur':dur0,'stock':5,'catch':Counter(),'coins':0,'offer':0,'baits':0} for _ in range(n_players)]
        decay_t=0
        for t in range(minutes*60):
            decay_t+=1
            if decay_t>=30:
                decay_t=0; pool=[x*0.9 for x in pool]
                if sum(pool)<1: pool=[0.0,0.0,0.0]
            for p in players:
                if p['timer']>0:
                    p['timer']-=1; continue
                if p['state']=='restock':
                    p['stock']=5; p['state']='idle'
                if p['state']=='idle':
                    if sum(pool)<40 and p['stock']>0:
                        v=GROUNDBAIT[bait]; pool=[pool[i]+v[i] for i in range(3)]
                        p['stock']-=1; p['baits']+=1; p['timer']=5
                        continue
                    if p['stock']==0 and sum(pool)<5:
                        p['state']='restock'; p['timer']=90; continue
                    p['state']='waiting'
                    p['timer']=max(1,int(random.expovariate(1/interval(sum(pool)))))
                elif p['state']=='waiting':
                    name = pick_fish(pool)
                    perfect = random.random()<0.25
                    r,tm,dmg,w = fight(C,S,p['dur'],name,n_cats=1,perfect=perfect)
                    p['dur']-=dmg; p['timer']=int(tm)+8   # +8s 抄网/收纳
                    if r=='landed':
                        p['dur']-=1
                        p['catch'][name]+=1
                        p['coins']+=max(1,round(w*10))
                        p['offer']+=FISH[name][4]
                    if r=='rodbreak' or p['dur']<=0:
                        p['dur']=dur0; p['coins']-=price; p['timer']+=45
                        nonlocal_breaks[0]+=1
                    p['state']='idle'
        for p in players:
            agg+=p['catch']; catch_counts.append(sum(p['catch'].values()))
            offer_counts.append(p['offer']); coin_counts.append(p['coins']); bait_used.append(p['baits'])
    return dict(catch=st.mean(catch_counts), offer=st.mean(offer_counts),
                coins=st.mean(coin_counts), baits=st.mean(bait_used),
                breaks=nonlocal_breaks[0]/runs, top=agg.most_common(5))

nonlocal_breaks=[0]

def model_b():
    print('\n'+'='*100)
    print('模型B｜一日渔获（每人每白天均值；河流区；ASSUMPTION 见文件头）')
    print('='*100)
    print(f'{"场景":<34}{"渔获/人":<9}{"供奉/人":<9}{"金币/人":<9}{"窝料/人":<9}{"断竿/局":<8}')
    scenarios = [
        (20,1,'普通猫',1,'发酵谷物窝','单人20min·1级竿·酵窝'),
        (20,1,'普通猫',1,'花果香窝','单人20min·1级竿·香窝'),
        (20,1,'普通猫',1,'虫虫窝',  '单人20min·1级竿·腥窝'),
        (20,4,'普通猫',1,'发酵谷物窝','4人20min·共享池·酵窝'),
        (20,8,'普通猫',1,'发酵谷物窝','8人20min·共享池·酵窝'),
        (30,1,'普通猫',1,'发酵谷物窝','单人30min·1级竿·酵窝'),
        (20,1,'缅因猫',3,'虫虫窝',  '单人20min·3级竿缅因·腥窝'),
    ]
    for minutes,n,cat,rod,bait,label in scenarios:
        nonlocal_breaks[0]=0
        r = day_sim(minutes,n,cat,rod,bait)
        print(f'{label:<34}{r["catch"]:<9.1f}{r["offer"]:<9.1f}{r["coins"]:<9.0f}{r["baits"]:<9.1f}{r["breaks"]:<8.2f}')
        if n==1 and bait=='发酵谷物窝' and minutes==20:
            print(f'    └ 鱼种TOP: {r["top"]}')
        if bait=='花果香窝' and n==1:
            print(f'    └ 鱼种TOP: {r["top"]}')

# ---------- 模型C：世界进度与额度节奏 ----------
def model_c():
    print('\n'+'='*100)
    print('模型C｜世界进度(满100)与10天毕业的耦合（用模型B吞吐反推）')
    print('='*100)
    nonlocal_breaks[0]=0
    solo = day_sim(20,1,'普通猫',1,'花果香窝')     # 供奉效率最高策略
    nonlocal_breaks[0]=0
    team4 = day_sim(20,4,'普通猫',1,'花果香窝')
    for label,r,n in [('单人·香窝',solo,1),('4人·香窝',team4,4)]:
        per_day_team = r['offer']*n
        days = 100/per_day_team if per_day_team>0 else 999
        print(f'{label}: 全献策略下 供奉点/天(全队)≈{per_day_team:.0f} → 攒满100进度仅需 {days:.1f} 天（目标=10天）')
    print('→ 若维持 10 天节奏：额度曲线+进度需求要消化 5~20 倍于此的冗余，或吃/卖分流后仍严重超发')

if __name__=='__main__':
    model_a()
    model_b()
    model_c()
