"""Five architectural hideouts, composed from visually inspected native assets.

Inlaid floors keep original UVs. Monument, colonnade, low rail and small dressing
have deliberate scale hierarchies. No light/particle simulations are assumed.
"""
import math


def create(row,stations,doors,pairs):
    def ring(r):return [(round(r*math.cos(math.radians(15+30*k)),3),round(r*math.sin(math.radians(15+30*k)),3)) for k in range(12)]
    def square_gates():return [(x,17) for x in (-20,-12,-4,4,12,20)]+[(x,z) for x in (-19,19) for z in (8,0,-8)]
    def inset(name,x,z,span,top=.04,yaw=0):
        r=row(name,x,z,span=span,yaw=yaw,walk=True);r.update(surface=True,surfaceTop=top);return r
    def mirrored(name,points,span,yaw=0,walk=False):return pairs(name,points,span=span,yaw=yaw,walk=walk)
    def portal_courts(spots,material='Circle_Statue',span=3):return [inset(material,x,z,span,top=.06) for x,z in spots]
    def plants(points,span=1.5):return mirrored('SM_Bush_010_a',points,span,walk=True)

    # 1. A coherent violet-marble palace: an inlaid 3x3 floor, twin apses and
    # matching columns/rails. Portal galleries stand outside the inner court.
    g=square_gates()
    royal=stations([(-7,-10),(0,-11),(7,-10)],(0,-19))+doors(g)
    royal += [inset('KV_Floor_01_I',x,z,12) for x in (-12,0,12) for z in (-12,0,12)]
    royal += [row('KV_Statue_01a',0,11,span=8)]
    royal += mirrored('KV_Wall_Circle_Third_Inner',[(9,10)],10,yaw=35)
    royal += mirrored('KV_Pillar_Small_Single_Cap_01',[(16,14),(16,4),(16,-4)],7)
    royal += mirrored('KV_Wall_Ark_Long',[(12,5)],8,yaw=90)
    royal += mirrored('KV_Railing_Small_Straight_01',[(12,-2),(12,-14)],7,yaw=90)
    royal += mirrored('SM_Urn_02a',[(10,-15),(13,8),(6,16)],1.6)
    royal += plants([(15,-10),(12,-16)],2.5)
    royal += mirrored('SM_Torch_01a',[(4,12)],2)
    royal += [inset('KV_Small_Circle_Floor_01',0,11,6,top=.065)]
    royal += mirrored('KV_Railing_Small_Curve_10',[(5,-18)],6)
    royal += mirrored('SM_Urn_01a',[(3,-14)],1.3)
    royal += [row('SM_Statue_01a',0,18,span=5)]
    royal += mirrored('Eterra Vase 02',[(10,-12)],2)

    # Layered, inhabited green court; architecture encloses the fountain and
    # shallow planting occupies the borders, never the service approaches.
    g=ring(22)
    garden=stations([(-7,-8),(0,-9),(7,-8)],(0,-16))+doors(g)
    garden += [inset('Floor_Stone_Circle',0,0,34),inset('CouncilChamberFloor_Eterra_01',0,-1,27,top=.07),row('Circle_Fountain',0,11,span=11)]
    garden += mirrored('Garden_Amphitheater_Big__round_Half',[(11,7)],8,yaw=40)
    garden += mirrored('Garden_Pillar_Big',[(10,17),(17,5),(16,-7)],6.5)
    garden += mirrored('H_Temple_Railings_01',[(12,-10),(17,1)],6.5,yaw=90)
    garden += plants([(16,10),(11,-8),(7,16),(17,-4)],3)
    garden += mirrored('Grass_Flower_PinkWhite_Clump',[(14,7),(7,-12),(14,-5),(8,14)],1.5,walk=True)
    garden += mirrored('Eterra Vase 02',[(12,-7),(11,11),(4,-16)],2.5)
    garden += [row('TOE_Railings_Turn_L1',x,z,span=3) for x,z in ((-4,-17),(-4,18))]
    garden += [row('TOE_Railings_Turn_L2',x,z,span=3) for x,z in ((4,-17),(4,18))]
    garden += [row('SM_Statue_01a',0,20,span=4)]+mirrored('SM_Torch_01a',[(5,9)],2)

    # Sea-temple terraces use original blue-and-gold paving, rope columns and
    # compact coral beds. The two six-unit causeways remain open.
    g=[(side*(35.1+8*math.cos(math.radians(30+k*60))),8*math.sin(math.radians(30+k*60))) for side in (-1,1) for k in range(6)]
    sea=stations([(-6,-6),(0,-7),(6,-6)],(0,-13))+doors(g)
    sea += [inset('Floor_Stone_Circle',0,0,28)]+[inset('Floor_Stone_Circle',x,0,19) for x in (-35.1,35.1)]
    sea += [inset('CouncilChamberFloor_Eterra_01',0,0,24,top=.07)]+[inset('CouncilChamberFloor_Eterra_01',x,0,16,top=.07) for x in (-35.1,35.1)]
    sea += mirrored('Coral_RedFlower_01',[(30,-3),(40,1),(10,-9)],1.3,walk=True)
    sea += [inset('Circle_Statue',0,10,7,top=.09)]+[inset('Circle_Statue',x,2,4,top=.09) for x in (-35,35)]
    sea += mirrored('LagonPillar_Thick',[(7,8),(35,3)],7)
    sea += mirrored('LagonPillar_Thin',[(11,4),(35,-4.5)],8)
    sea += [row('CrystalCluster',0,10,span=7)]
    sea += mirrored('Coral_16_G1_BlueVSPro',[(31,5),(39,-4),(12,8),(39,4)],3.5,walk=True)
    sea += mirrored('Eterra Vase 03',[(32,-6),(38,6),(8,-10)],2)
    sea += mirrored('H_Temple_Railings_01',[(8,-10),(11,11)],4,yaw=90)
    sea += [row('Astrolabe',0,14,span=4),row('Coral_RedFlower_01',0,12.5,span=1,walk=True)]

    # A continuous tiled workshop floor, carved rear arcade, and large bronze
    # forge establish an actual gallery rather than three empty circular pads.
    g=[(x,z) for z in (7,-2) for x in (-28,-18,-8,8,18,28)]
    forge=stations([(-7,-9),(0,-9),(7,-9)],(0,-13))+doors(g)
    forge += [inset('KV_Floor_02_I',x,z,12) for x in (-24,-12,0,12,24) for z in (-5,5)]
    forge += [row('Client_Forge',0,8.5,span=6.5)]
    forge += [row('Cemetery_Column',x,12,span=6.5) for x in (-30,-18,-6,6,18,30)]
    forge += [row('KV_Wall_Ark_Long',x,12.5,span=10) for x in (-25,-9,9,25)]
    forge += mirrored('H_Temple_Railings_01',[(22,-12),(10,-12)],7)
    forge += mirrored('SM_Urn_02a',[(4,-11),(14,0),(31,-9)],1.5)
    forge += mirrored('SM_Torch_01a',[(24,11),(4,9)],2)
    forge += mirrored('SM_Statue_01a',[(34,4),(34,-6)],4)
    forge += mirrored('Eterra Vase 02',[(30,0),(12,-10)],2.5)
    forge += [inset('KV_Small_Circle_Floor_01',0,8.5,8,top=.065)]

    # The celestial court shares a stone family with the palace, but has a
    # circular composition, orrery, crystalline rear sanctuary and curved edges.
    g=ring(22)
    astral=stations([(-7,-8),(0,-9),(7,-8)],(0,-16))+doors(g)
    astral += [inset('Floor_Stone_Circle',0,0,34),inset('KV_Floor_Circle_01',0,0,33,top=.065),inset('KV_Small_Circle_Floor_01',0,9,11,top=.085),row('Astrolabe',0,9,span=7)]
    astral += mirrored('KV_Statue_01a',[(8,11)],6)
    astral += mirrored('KV_Pillar_Small_Single_Cap_01',[(12,14),(17,1),(14,-8)],7)
    astral += mirrored('KV_Wall_Circle_Third_Inner',[(7,13)],9,yaw=45)
    astral += mirrored('KV_Wall_Ark_Long',[(12,6)],8,yaw=60)
    astral += mirrored('CrystalCluster',[(12,6),(5,17)],4)
    astral += mirrored('SM_Urn_04a',[(6,-12),(13,7)],2)
    astral += mirrored('KV_Railing_Small_Curve_10',[(6,-13),(13,-6),(13,2)],5,yaw=90)
    astral += mirrored('SM_Torch_01a',[(11,10),(4,-13)],2)
    astral += plants([(15,5),(10,-11),(15,-13)],2.5)
    astral += mirrored('Eterra Vase 03',[(8,-5),(11,15)],2.5)

    specs=[('verdant-sanctum','The Moonstone Court','square','Cracked stone',royal,'Violet marble, vaulted apses and a guardian statue form a finished palace courtyard. An inlaid floor leads to a compact service court; the portals sit in separate outer galleries.','#c2b0dc'),
           ('ember-foundry','The Verdant Exedra','round','Cracked stone',garden,'A stepped fountain anchors a formal green garden. Paired colonnades, planted ceramic bowls and small flowering beds frame the court without covering its walking routes.','#a6bf82'),
           ('tidebound-archive','The Tideglass Terraces','islands','Cracked stone',sea,'Blue-and-gold rope columns turn three islands into a sea-temple sanctuary. Coral borders and paved portal courts enclose the side terraces while both connecting bridges remain clear.','#77bac8'),
           ('ossuary-court','The Sunforge Gallery','hall','Grey rubble',forge,'A bronze forge and ordered stone colonnade define three workshop bays. Small urns, dark metal braziers and low carved rails give the gallery a deliberate, inhabited rhythm.','#c8a26c'),
           ('astral-conservatory','The Celestial Rotunda','round','Cracked stone',astral,'A brass orrery rises between marble guardians and vaulted alcoves. Layered circular paving, restrained crystal clusters and low foreground details create a calm ceremonial observatory.','#a9add6')]
    result=[]
    for ident,label,shape,texture,items,description,accent in specs:
        assert len(items)==60,(ident,len(items))
        result.append(dict(id=ident,label=label,shape=shape,size=26,texture=texture,accent=accent,description=description,items=items))
    return result
