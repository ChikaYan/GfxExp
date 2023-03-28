import os
from py_script.log_parser import log_parse

def cmd(command):
    print(command)
    os.system(command)


exp_name = 'test'

# c = f"C:\\nonsys\\workspace\\GfxExp\\build\\bin\\Debug\\neural_radiance_caching.exe \
#     -cam-pos -0.9, 0.93, 2.586 \
#     -cam-roll 7.487 -cam-pitch -cam-yaw -cam-yaw 77.589 \
#     -brightness 2.0 \
#     -name scene -obj C:\\nonsys\\workspace\\GfxExp\\scene\\living_room\\living_room.obj 1.0 simple_pbr \
#     -inst scene \
#     -name rectlight0 -emittance 1200 0 0 -rectangle 0.1 0.1 \
#     -begin-pos -0.2 1.05 2.586 -end-pos -0.2 1.05 3.0 -freq 5 -time 1.0 -inst rectlight0 \
#     -exp_name {exp_name} -nrc_only -frame_num 50 -save_img_every 1 \
# "
# c = f"C:\\nonsys\\workspace\\GfxExp\\build\\bin\\Debug\\restir.exe \
#     -cam-pos -0.9, 0.93, 2.586 \
#     -cam-roll 7.487 -cam-pitch -cam-yaw -cam-yaw 77.589 \
#     -brightness 2.0 \
#     -name scene -obj C:\\nonsys\\workspace\\GfxExp\\scene\\living_room\\living_room.obj 1.0 simple_pbr \
#     -inst scene \
#     -name rectlight0 -emittance 1200 0 0 -rectangle 0.1 0.1 \
#     -begin-pos -0.2 1.05 2.586 -end-pos -0.2 1.05 3.0 -freq 5 -time 1.0 -inst rectlight0 \
# "

c = f"C:\\nonsys\\workspace\\GfxExp\\build\\bin\\Debug\\neural_radiance_caching.exe \
    -cam-pos -0.9, 0.93, 2.586 \
    -cam-roll 7.487 -cam-pitch -cam-yaw -cam-yaw 77.589 \
    -brightness 2.0 \
    -name scene -obj C:\\nonsys\\workspace\\GfxExp\\scene\\living_room\\living_room.obj 1.0 simple_pbr \
    -inst scene \
    -name rectlight0 -emittance 1200 0 0 -rectangle 0.1 0.1 \
    -begin-pos -0.2 1.05 2.586 -end-pos -0.2 1.05 3.0 -freq 5 -time 1.0 -inst rectlight0 \
    -exp_name test -frame_num -1 -save_img_every 0 \
"

# c = f"C:\\nonsys\\workspace\\GfxExp\\build\\bin\\Debug\\neural_radiance_caching.exe \
#     -cam-pos -0.9, 0.93, 2.586 \
#     -cam-roll 7.487 -cam-pitch -cam-yaw -cam-yaw 77.589 \
#     -brightness 2.0 \
#     -name scene -obj C:\\nonsys\\workspace\\GfxExp\\scene\\living_room\\living_room.obj 1.0 simple_pbr \
#     -inst scene \
#     -name rectlight0 -emittance 1200 0 0 -rectangle 0.1 0.1 \
#     -begin-pos -0.2 1.05 2.586 -end-pos -0.2 1.05 3.0 -freq 5 -time 1.0 -inst rectlight0 \
#     -exp_name pt_spp1 -no_nrc -max_path_len 20 -spp 1 \
# "

cmd(c)

# log_parse(exp_path=f'C:\\nonsys\\workspace\\GfxExp\\exp\\{exp_name}')
