import os
import imageio
import numpy as np
from pathlib import Path
from py_script.log_parser import log_parse
import yaml

def cmd(command):
    print(command)
    os.system(command)



spp = 64
exp_name = f'bedroom/pt_{spp}'
c = f"C:\\nonsys\\workspace\\GfxExp\\build\\bin\\Debug\\neural_radiance_caching.exe \
    -cam-pos 12.533, -17.324, 24.736 \
    -cam-roll -171.449 -cam-pitch -103.079 -cam-yaw -0.946 \
    -brightness 1.0 \
    -name scene -obj C:\\nonsys\\workspace\\GfxExp\\scene\\bedroom\\iscv2.obj 1.0 simple_pbr \
    -inst scene \
    -name rectlight0 -emittance 1200 0 0 -rectangle 0.1 0.1 \
    -begin-pos -0.2 1.05 2.586 -end-pos -0.2 1.05 3.0 -freq 5 -time 1.0 -inst rectlight0 \
    -exp_name {exp_name} -frame_num 50 -save_img_every 1 -unbiased_restir -no_nrc -max_path_len 20 -spp {spp} \
"

cmd(c)

with open(f"./exp/{exp_name}/cmd.txt", "w") as f:
    f.write(c)

log_parse(exp_path=f'C:\\nonsys\\workspace\\GfxExp\\exp\\{exp_name}', log_gt=True)

# spp = 64

# # for i in range(spp):

# #     c = f"C:\\nonsys\\workspace\\GfxExp\\build\\bin\\Debug\\neural_radiance_caching.exe \
# #         -cam-pos -0.9, 0.93, 2.586 \
# #         -cam-roll 7.487 -cam-pitch -cam-yaw -cam-yaw 77.589 \
# #         -brightness 2.0 \
# #         -name scene -obj C:\\nonsys\\workspace\\GfxExp\\scene\\living_room\\living_room.obj 1.0 simple_pbr \
# #         -inst scene \
# #         -name rectlight0 -emittance 1200 0 0 -rectangle 0.1 0.1 \
# #         -begin-pos -0.2 1.05 2.586 -end-pos -0.2 1.05 3.0 -freq 5 -time 1.0 -inst rectlight0 \
# #         -exp_name pt{i:02d} -no_nrc -rnd_seed {i:05d} -max_path_len 20 \
# #     "

# #     cmd(c)

# exp_name = f'pt_{spp}_test'

# c = f"C:\\nonsys\\workspace\\GfxExp\\build\\bin\\Debug\\neural_radiance_caching.exe \
#     -cam-pos -0.9, 0.93, 2.586 \
#     -cam-roll 7.487 -cam-pitch -cam-yaw -cam-yaw 77.589 \
#     -brightness 2.0 \
#     -name scene -obj C:\\nonsys\\workspace\\GfxExp\\scene\\living_room\\living_room.obj 1.0 simple_pbr \
#     -inst scene \
#     -name rectlight0 -emittance 1200 0 0 -rectangle 0.1 0.1 \
#     -begin-pos -0.2 1.05 2.586 -end-pos -0.2 1.05 3.0 -freq 5 -time 1.0 -inst rectlight0 \
#     -exp_name {exp_name} -no_nrc -max_path_len 20 -spp {spp} \
# "

# cmd(c)

# out_path = Path(f'C:\\nonsys\\workspace\\GfxExp\\exp\\{exp_name}_avg\\imgs')
# out_path.mkdir(exist_ok=True, parents=True)

# for img_i in range(50):
#     imgs = []
#     for i in range(spp):
#         # img_path = f"C:\\nonsys\\workspace\\GfxExp\\exp\\pt{i:02d}\\imgs\\{img_i:05d}.png"
#         img_path = f"C:\\nonsys\\workspace\\GfxExp\\exp\\{exp_name}\\imgs\\{img_i:05d}_{i:02d}.png"
#         im = imageio.imread(img_path)
#         imgs.append(im)

#     imgs = np.stack(imgs) / 255.
#     im = (imgs.mean(axis=0) * 255.).astype(np.uint8)

#     imageio.imwrite(str(out_path / f'{img_i:05d}.png'), im)


