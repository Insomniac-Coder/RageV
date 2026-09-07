#!/bin/bash
# Four converged end-pose stills: footprint / old resolve x clamp on / off.
cd /c/Users/ism19/Code/RageV || exit 1
ST=build/bin/Release/RageVRuntime/assets/shaders; SRC=RageVEditor/assets/shaders; D=tools/scripts/garage/session_2026_09_06
python $D/make_old_resolve.py $D/reflection_resolve_v5.rvshader || exit 1
python - <<'PY'
p='C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_accumulate.rvshader'; s=open(p,'rb').read().decode('utf-8')
a='const vec3 held = clamp(c.past.rgb, mean - halfWidth, mean + halfWidth);'; assert s.count(a)==1
open('C:/Users/ism19/Code/RageV/tools/scripts/garage/session_2026_09_06/reflection_accumulate_unclamped.rvshader','wb').write(s.replace(a,'const vec3 held = c.past.rgb;').encode('utf-8')); print('unclamped accumulator written')
PY
render() { python tools/scripts/garage/burst.py $1 --speed=1.5 --stop=2.0 --frames=1 --from=400 2>&1 | tail -1; }
echo "[a] v6 clamped";            cp $SRC/reflection_resolve.rvshader $ST/; cp $SRC/reflection_accumulate.rvshader $ST/; render endpose_v6
echo "[b] v6 unclamped";          cp $D/reflection_accumulate_unclamped.rvshader $ST/reflection_accumulate.rvshader; render endpose_v6_unclamped
echo "[c] old resolve unclamped"; cp $D/reflection_resolve_v5.rvshader $ST/reflection_resolve.rvshader; render endpose_old_unclamped
echo "[d] old resolve clamped";   cp $SRC/reflection_accumulate.rvshader $ST/; render endpose_old_clamped
cp $SRC/reflection_resolve.rvshader $ST/; cp $SRC/reflection_accumulate.rvshader $ST/
cmp $ST/reflection_resolve.rvshader $SRC/reflection_resolve.rvshader && cmp $ST/reflection_accumulate.rvshader $SRC/reflection_accumulate.rvshader && echo "staged restored == source"
ls SampleProject/assets/scenes | grep burst || echo "no burst copy"
echo DONE
