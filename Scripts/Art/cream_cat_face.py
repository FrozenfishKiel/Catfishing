"""Designed feline cross-sections, cupped ears and projected blinking eyelids.

Call build_face with the main builder callbacks, then bind_blink(rig).
Animate eye.L/R custom property ["blink"] from 0 to 1; keep eye bone scale 1.
"""
import math
import bmesh
import bpy

_BLINK_OBJECTS = []


def smooth(a, b, value):
    t = max(0.0, min(1.0, (value-a)/(b-a)))
    return t*t*(3-2*t)


def mix(a, b, t):
    return tuple(x*(1-t)+y*t for x,y in zip(a,b))


def gauss(x,z,x0,z0,w,h):
    return math.exp(-((x-x0)/w)**2-((z-z0)/h)**2)


def profile(points,z):
    if z <= points[0][0]: return points[0][1]
    if z >= points[-1][0]: return points[-1][1]
    for i in range(len(points)-1):
        a,b=points[i],points[i+1]
        if a[0] <= z <= b[0]:
            before,after=points[max(0,i-1)],points[min(len(points)-1,i+2)]
            span=b[0]-a[0]; t=(z-a[0])/span
            m0=(b[1]-before[1])/(b[0]-before[0])*span
            m1=(after[1]-a[1])/(after[0]-a[0])*span
            return max(0,(2*t**3-3*t*t+1)*a[1]+(t**3-2*t*t+t)*m0+(-2*t**3+3*t*t)*b[1]+(t**3-t*t)*m1)


def round_outline(points,samples):
    result=[]
    for i in range(samples):
        position=i*len(points)/samples; j,t=int(position),position%1
        controls=[points[(j+k)%len(points)] for k in (-1,0,1,2)]
        result.append(tuple(.5*(2*b+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t**3) for a,b,c,d in zip(*controls)))
    return result


def bind_blink(rig):
    """Wire shape keys to simple bone-property drivers; return driven mesh count."""
    for suffix in ('L','R'):
        bone=rig.pose.bones['eye.'+suffix]
        bone['blink']=0.0
        bone.id_properties_ui('blink').update(min=0.0,max=1.0,soft_min=0.0,soft_max=1.0)
        bone.scale=(1,1,1)
    for obj,suffix in _BLINK_OBJECTS:
        driver=obj.data.shape_keys.key_blocks['Blink'].driver_add('value').driver
        driver.type='SCRIPTED'
        while driver.variables:
            driver.variables.remove(driver.variables[0])
        variable=driver.variables.new(); variable.name='b'; variable.type='SINGLE_PROP'
        variable.targets[0].id=rig
        variable.targets[0].data_path='pose.bones["eye.'+suffix+'"]["blink"]'
        driver.expression='max(0.0, min(1.0, b))'
    return len(_BLINK_OBJECTS)


def build_face(ellipsoid,curve_mesh,finish,colors):
    """Build one closed sculpted head and register all head/eye/ear skin parts."""
    _BLINK_OBJECTS.clear()
    cy=-.027
    widths=[(.756,0),(.800,.119),(.847,.216),(.901,.296),(.954,.350),(1.014,.384),(1.080,.381),(1.148,.355),(1.216,.314),(1.277,.257),(1.327,.178),(1.378,0)]
    fronts=[(.756,0),(.825,.161),(.905,.252),(.981,.285),(1.067,.288),(1.145,.271),(1.228,.221),(1.306,.144),(1.378,0)]
    backs=[(.756,0),(.825,.132),(.915,.216),(1.025,.251),(1.147,.232),(1.256,.188),(1.328,.114),(1.378,0)]

    def rounded_profile(points,z):
        """Use ellipsoidal pole curvature beyond the unchanged face-feature band.

        A radius proportional to sqrt(distance from pole), rather than distance,
        gives a rounded cap with a horizontal crown tangent.  Blend derivatives
        into the original cheek/forehead profiles away from the eyes and mouth.
        """
        original=profile(points,z)
        if z > 1.20:
            center,top,anchor=1.10,1.378,1.23
            height=top-center
            unit=math.sqrt(max(0,1-((z-center)/height)**2))
            anchor_unit=math.sqrt(1-((anchor-center)/height)**2)
            cap=profile(points,anchor)*unit/anchor_unit
            return original+(cap-original)*smooth(1.20,1.275,z)
        if z < .945:
            center,bottom,anchor=1.035,.756,.925
            height=center-bottom
            unit=math.sqrt(max(0,1-((z-center)/height)**2))
            anchor_unit=math.sqrt(1-((anchor-center)/height)**2)
            cap=profile(points,anchor)*unit/anchor_unit
            return cap+(original-cap)*smooth(.855,.945,z)
        return original

    def width(z):
        # A single broad cheek curve, without stacked horizontal fur ridges.
        return rounded_profile(widths,z)

    def sculpt(x,z):
        pads=sum(.056*gauss(x,z,s*.075,.985,.078,.069) for s in (-1,1))
        bridge=.011*gauss(x,z,0,1.042,.047,.106)
        chin=.021*gauss(x,z,0,.918,.102,.055)
        groove=.006*gauss(x,z,0,.977,.018,.048)
        sockets=sum(.010*gauss(x,z,s*.145,1.097,.090,.088) for s in (-1,1))
        return pads+bridge+chin-groove-sockets

    def surface_y(x,z):
        side=math.sqrt(max(0,1-(x/max(.00001,width(z)))**2))
        exponent=.70+.20*smooth(1.13,1.28,z)
        # The chin returns to an oval section instead of carrying the flatter
        # muzzle plane around the jaw as a box-shaped shelf.
        exponent=1.0+(exponent-1.0)*smooth(.845,.945,z)
        return cy-rounded_profile(fronts,z)*side**exponent-sculpt(x,z)*smooth(0,.55,side)

    def paint_head(v):
        x,y,z=v
        base=mix(colors['cream'],colors['milk'],.50)
        front=1-smooth(-.10,.09,y)
        base=mix(base,mix(colors['cream'],colors['biscuit'],.50),smooth(1.17,1.31,z)*.60)
        tiger=0.0
        for sign in (-1,0,1):
            center=sign*(.084+.21*(z-1.25))+.009*math.sin((z-1.2)*24)
            taper=math.sin(math.pi*smooth(1.193,1.355,z))
            tiger=max(tiger,(1-smooth(.006,.017,abs(x-center)))*taper)
        for level in (1.069,1.020):
            line=level-.31*(abs(x)-.254)
            stripe=(1-smooth(.004,.012,abs(z-line)))*smooth(.238,.266,abs(x))*(1-smooth(.331,.380,abs(x)))
            tiger=max(tiger,stripe)
        base=mix(base,mix(colors['biscuit'],colors['ginger'],.20),tiger*front*.62)
        muzzle=(x/.190)**2+((z-.956)/.125)**2
        return mix(base,colors['milk'],(1-smooth(.66,1.18,muzzle))*front)

    def mesh_object(name,verts,faces,mat,weight,paint=None,blink=None,suffix=None):
        mesh=bpy.data.meshes.new(name); mesh.from_pydata(verts,[],faces)
        bm=bmesh.new(); bm.from_mesh(mesh)
        bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces)); bm.to_mesh(mesh); bm.free()
        obj=bpy.data.objects.new(name,mesh); bpy.context.collection.objects.link(obj)
        finish(obj,mat,weight,paint)
        if blink is not None:
            obj.shape_key_add(name='Basis'); key=obj.shape_key_add(name='Blink')
            for vertex,target in zip(key.data,blink): vertex.co=target
            _BLINK_OBJECTS.append((obj,suffix))
        return obj

    # Closed cross-section mesh: broad cheek, narrower chin, flatter face plane.
    # This is also the projection source for the fused head/body surface.
    # Dense latitude sampling prevents that projection from copying coarse bands.
    n,rings=128,128; verts=[(0,cy,.756)]
    for j in range(1,rings):
        z=.756+(.5-.5*math.cos(math.pi*j/rings))*(1.378-.756)
        for i in range(n):
            angle=2*math.pi*i/n; x=width(z)*math.sin(angle)
            y=surface_y(x,z) if math.cos(angle)>=0 else cy-rounded_profile(backs,z)*math.cos(angle)
            verts.append((x,y,z))
    top=len(verts); verts.append((0,cy,1.378))
    faces=[(0,1+(i+1)%n,1+i) for i in range(n)]
    faces += [(1+j*n+i,1+j*n+(i+1)%n,1+(j+1)*n+(i+1)%n,1+(j+1)*n+i) for j in range(rings-2) for i in range(n)]
    faces += [(top,top-n+i,top-n+(i+1)%n) for i in range(n)]
    head=mesh_object('Cat_Head',verts,faces,'Fur','head',paint_head)

    # Closed, thin ear cups with three small soft fur lobes sculpted inside.
    outline=round_outline([(.120,1.199),(.144,1.306),(.229,1.461),(.254,1.488),(.277,1.471),(.302,1.415),(.363,1.245),(.338,1.191),(.233,1.176)],64)
    ear_rings=[(.05,.026),(.75,.023),(1,-.016),(.94,-.061),(.80,-.066),(.53,-.021),(.25,.004),(.02,.009)]
    for sign,suffix in ((1,'L'),(-1,'R')):
        verts,shades=[],[]
        for ring,(size,depth) in enumerate(ear_rings):
            for x0,z0 in outline:
                x,z=.244+(x0-.244)*size,1.320+(z0-1.320)*size
                fur=max(gauss(x,z,.200,1.327,.026,.042),gauss(x,z,.251,1.314,.025,.047),gauss(x,z,.295,1.300,.026,.038)) if ring>=4 else 0
                verts.append((sign*x,depth-.017*fur,z))
                outer=mix(colors['cream'],colors['biscuit'],.52); inner=mix(colors['pink'],colors['milk'],.46)
                color=outer if ring<3 else mix(outer,inner,min(1,(ring-2)*.53))
                shades.append(mix(color,colors['milk'],fur*.80))
        n=len(outline); faces=[tuple(reversed(range(n)))]
        faces += [(r*n+i,r*n+(i+1)%n,(r+1)*n+(i+1)%n,(r+1)*n+i) for r in range(len(ear_rings)-1) for i in range(n)]
        faces.append(tuple(range((len(ear_rings)-1)*n,len(ear_rings)*n)))
        ear=mesh_object('Ear.'+suffix,verts,faces,'Fur','ear.'+suffix)
        for i,color in enumerate(shades): ear.data.color_attributes['Color'].data[i].color=(*color,1)
        modifier=ear.modifiers.new('Round thin ear cup','SUBSURF'); modifier.levels=1
        bpy.ops.object.modifier_apply(modifier=modifier.name)

    eye_z,eye_width,eye_height=1.097,.075,.068
    for sign,suffix in ((1,'L'),(-1,'R')):
        x0=sign*.145; weight='eye.'+suffix

        def closure_z(x):
            u=(x-x0)/eye_width
            return eye_z+sign*.008*u-.006+.008*u*u

        def eye_front(x,z):
            tilt=sign*.008*(x-x0)/eye_width
            r2=((x-x0)/eye_width)**2+((z-eye_z-tilt)/eye_height)**2
            return surface_y(x,z)+.0015-.009*math.sqrt(max(0,1-r2))

        def hidden_blink(x,z):
            target_z=closure_z(x)+(z-closure_z(x))*.012
            return (x,surface_y(x,target_z)+.003,target_z)

        def lens(name,sx,sz,mat,paint,offset=0):
            n,radial=64,18
            verts=[(x0,eye_front(x0,eye_z)+offset,eye_z)]
            for j in range(1,radial+1):
                r=j/radial
                for i in range(n):
                    a=2*math.pi*i/n; x=x0+sx*r*math.cos(a)
                    z=eye_z+sz*r*math.sin(a)*(.87+.13*abs(math.sin(a)))+sign*.008*(x-x0)/eye_width
                    verts.append((x,eye_front(x,z)+offset,z))
            back=len(verts); verts.append((x0,surface_y(x0,eye_z)+.018,eye_z))
            faces=[(0,1+i,1+(i+1)%n) for i in range(n)]
            faces += [(1+j*n+i,1+j*n+(i+1)%n,1+(j+1)*n+(i+1)%n,1+(j+1)*n+i) for j in range(radial-1) for i in range(n)]
            faces += [(back,1+(radial-1)*n+(i+1)%n,1+(radial-1)*n+i) for i in range(n)]
            return mesh_object(name,verts,faces,mat,weight,paint,[hidden_blink(x,z) for x,y,z in verts],suffix)

        def iris_color(v):
            x,y,z=v; r=math.sqrt(((x-x0)/eye_width)**2+((z-eye_z)/eye_height)**2)
            amber=mix(colors.get('gold',colors['biscuit']),colors['biscuit'],.45)
            amber=mix(amber,colors['cocoa'],.18+.14*smooth(eye_z-.025,eye_z+.045,z))
            return mix(amber,colors['eye'],smooth(.80,.98,r))

        lens('EyeIris.'+suffix,eye_width,eye_height,'Eye',iris_color)
        lens('EyePupil.'+suffix,.039,.056,'Eye',lambda v:colors['eye'],-.0011)

        # A continuous rim contains both lids. Closure retains rim thickness,
        # follows the head surface, and places the iris/pupil behind that surface.
        verts,blink,shades=[],[],[]; n,cross,thickness=96,8,.0034
        for i in range(n):
            angle=2*math.pi*i/n; x=x0+eye_width*math.cos(angle)
            z=eye_z+eye_height*math.sin(angle)*(.87+.13*abs(math.sin(angle)))+sign*.008*(x-x0)/eye_width
            for j in range(cross):
                around=2*math.pi*j/cross
                dx,dz=thickness*math.cos(around)*math.cos(angle),thickness*math.cos(around)*math.sin(angle)
                px,pz=x+dx,z+dz; front_offset=-.0018-thickness*.55*math.sin(around)
                verts.append((px,surface_y(px,pz)+front_offset,pz))
                close_z=closure_z(px)+dz
                blink.append((px,surface_y(px,close_z)+front_offset,close_z))
                color=paint_head((px,surface_y(px,pz),pz))
                shades.append(mix(color,colors['cocoa'],smooth(.15,.75,-math.cos(around))*.48))
        faces=[(i*cross+j,((i+1)%n)*cross+j,((i+1)%n)*cross+(j+1)%cross,i*cross+(j+1)%cross) for i in range(n) for j in range(cross)]
        lid=mesh_object('Eyelids.'+suffix,verts,faces,'Fur',weight,blink=blink,suffix=suffix)
        for i,color in enumerate(shades): lid.data.color_attributes['Color'].data[i].color=(*color,1)
        for index,(dx,dz,size) in enumerate(((-.015,.019,.0072),(.014,-.018,.0028))):
            x,z=x0+dx,eye_z+dz
            obj=ellipsoid('EyeGlint.'+suffix+str(index),(x,eye_front(x,z)-.0022,z),(size,.0018,size*1.07),'Highlight',weight,segments=24,rings=16)
            obj.shape_key_add(name='Basis'); key=obj.shape_key_add(name='Blink')
            for vertex in key.data: vertex.co=hidden_blink(vertex.co.x,vertex.co.z)
            _BLINK_OBJECTS.append((obj,suffix))

    # A volumetric heart-shaped nose; subtle nostrils are sculpted depressions.
    nose_z=1.017
    nose_outline=round_outline([(0,-.019),(-.019,-.002),(-.025,.010),(-.021,.019),(-.010,.020),(0,.012),(.010,.020),(.021,.019),(.025,.010),(.019,-.002)],64)
    verts=[(0,surface_y(0,nose_z)-.018,nose_z)]
    for j in range(1,9):
        r=j/8
        for x,dz in nose_outline:
            x,z=x*r,nose_z+dz*r
            nostril=sum(gauss(x,z,s*.014,nose_z-.002,.004,.0028) for s in (-1,1))
            verts.append((x,surface_y(x,z)-.003-.016*math.sqrt(max(0,1-r*r))+.002*nostril,z))
    back=len(verts); verts.append((0,surface_y(0,nose_z)+.010,nose_z))
    faces=[(0,1+i,1+(i+1)%64) for i in range(64)]
    faces += [(1+j*64+i,1+j*64+(i+1)%64,1+(j+1)*64+(i+1)%64,1+(j+1)*64+i) for j in range(7) for i in range(64)]
    faces += [(back,1+7*64+(i+1)%64,1+7*64+i) for i in range(64)]

    def nose_color(v):
        nostril=max(gauss(v.x,v.z,s*.014,nose_z-.002,.004,.0028) for s in (-1,1))
        return mix(mix(colors['pink'],colors['milk'],.16),colors['cocoa'],nostril*.33)

    mesh_object('Nose',verts,faces,'InnerEar','head',nose_color)

    def lip_curve(name,points,radius,radii):
        curve_mesh(name,[(x,surface_y(x,z)-.0017,z) for x,z in points],radius,'Cocoa','head',radii=radii)

    lip_curve('Philtrum',[(0,.999),(0,.986),(0,.980)],.0019,[.35,1,1])
    for sign,suffix in ((1,'L'),(-1,'R')):
        lip_curve('Lip.'+suffix,[(0,.980),(sign*.025,.970),(sign*.051,.975),(sign*.063,.989)],.0019,[1,1,.65,.1])
        for j in range(2):
            x,z=sign*.207,.991-j*.025
            curve_mesh('Whisker.'+suffix+str(j),[(x,surface_y(x,z)-.002,z),(sign*.302,-.303,z+.013-j*.009),(sign*.395,-.286,z+.030-j*.019)],.0015,'Highlight','head',radii=[.25,.8,.06])
    head['FaceDesign']='Continuous cheek/chin sections, raised muzzle pads, recessed almond sockets, thin cupped ears'
    head['BlinkControl']='eye.L and eye.R custom property blink; eye scale remains 1'
    return head
