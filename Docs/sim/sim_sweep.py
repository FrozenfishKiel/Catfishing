# -*- coding: utf-8 -*-
"""模型D｜力量系数K整体缩放扫描：搏斗生态在什么倍率下恢复分层（巨影单列不缩放）"""
FISH = {
    '河纹鱼':(3,1.2,30),'小银鱼':(0.4,0.8,15),'小彩鱼':(0.4,1.0,25),
    '森林长尾鱼':(8,2.0,90),'花瓣鱼':(0.4,0.9,20),'风铃鱼':(0.4,0.9,20),
    '咸鱼':(1.2,1.5,45),'臭臭鱼':(0.4,1.8,75),'黑鱼':(8,2.3,100),
    '泥鳅':(0.5,0.7,15),'河口鲈':(5,1.8,80),
}
CATS = {'普通猫':50,'狸花猫':90,'缅因猫':130}
DUR,S = 120,120   # 3级竿

def zone(C,F,stam):
    if C>=2*F and S>F: return '碾压'
    if C>F and S>F:
        t_fish = stam/(0.08*C); t_rod = DUR/(0.1*F); t_cat = 100/(0.12*F)
        if t_fish < min(t_rod,t_cat): return '消耗战胜'
        return '消耗战险'      # 需要循环回体力/距离战，有翻车可能
    return '拉不动'            # 鱼力>=猫力：只能放线周旋或合力

for scale in (1,3,5,8):
    print(f'\n=== K×{scale}（按最大体重算鱼力；3级竿120/120） ===')
    for cat,C in CATS.items():
        zones = {}
        for name,(wmax,K,stam) in FISH.items():
            F = wmax*K*scale
            zones.setdefault(zone(C,F,stam),[]).append(f'{name}({F:.0f})')
        line = f'{cat:<5}: '
        for z in ('碾压','消耗战胜','消耗战险','拉不动'):
            if z in zones: line += f'[{z}:{len(zones[z])}] '
        print(line)
        for z in ('消耗战胜','消耗战险','拉不动'):
            if z in zones: print(f'    {z}: {", ".join(zones[z])}')
