import os

def cmd(command):
    print(command)
    os.system(command)


# exp_name = 'bedroom/full_bias'

# c = f"C:\\nonsys\\workspace\\GfxExp\\build\\bin\\Debug\\neural_radiance_caching.exe \
#     -cam-pos -0.9, 0.93, 2.586 \
#     -cam-roll 7.487 -cam-pitch -cam-yaw -cam-yaw 77.589 \
#     -brightness 2.0 \
#     -name scene -obj C:\\nonsys\\workspace\\GfxExp\\scene\\living_room\\living_room.obj 1.0 simple_pbr \
#     -inst scene \
#     -name rectlight0 -emittance 1200 0 0 -rectangle 0.1 0.1 \
#     -begin-pos -0.2 1.05 2.586 -end-pos -0.2 1.05 3.0 -freq 5 -time 1.0 -inst rectlight0 "

exp_name = 'bedroom/nrc_grid'
c = f"C:\\nonsys\\workspace\\GfxExp\\build\\bin\\Debug\\neural_radiance_caching.exe \
    -cam-pos 12.533, -17.324, 24.736 \
    -cam-roll -171.449 -cam-pitch -103.079 -cam-yaw -0.946 \
    -brightness 1.0 \
    -name scene -obj C:\\nonsys\\workspace\\GfxExp\\scene\\bedroom\\iscv2.obj 1.0 simple_pbr \
    -inst scene \
    -name rectlight0 -emittance 1200 0 0 -rectangle 0.1 0.1 \
    -begin-pos -0.2 1.05 2.586 -end-pos -0.2 1.05 3.0 -freq 5 -time 1.0 -inst rectlight0 "
c = f"C:\\nonsys\\workspace\\GfxExp\\build\\bin\\Debug\\neural_radiance_caching.exe \
    -cam-pos 0, 0, 0 \
    -cam-roll -171.449 -cam-pitch -103.079 -cam-yaw -0.946 \
    -brightness 1.0 \
    -name scene -obj C:/nonsys/workspace/GfxExp/scene/ZeroDay_v1/MEASURE_SEVEN/MEASURE_SEVEN_COLORED_LIGHTS.fbx 1.0 simple_pbr \
    -inst scene \
    -name rectlight0 -emittance 1200 0 0 -rectangle 0.1 0.1 \
    -begin-pos -0.2 1.05 2.586 -end-pos -0.2 1.05 3.0 -freq 5 -time 1.0 -inst rectlight0 "

# c += f"-exp_name {exp_name} -frame_num 50 -save_img_every 5 "
# c += f"-unbiased_restir "
# c += f"-nrc_only "
# c += f"-position-encoding hash-grid "
# c += f"-exp_name test -frame_num -1 -save_img_every 0 -unbiased_restir"
# c += f"-exp_name test -frame_num -1 -save_img_every 0 -unbiased_restir"



# c = f"C:\\nonsys\\workspace\\GfxExp\\build\\bin\\Debug\\restir.exe \
#     -cam-pos -0.9, 0.93, 2.586 \
#     -cam-roll 7.487 -cam-pitch -cam-yaw -cam-yaw 77.589 \
#     -brightness 2.0 \
#     -name scene -obj C:\\nonsys\\workspace\\GfxExp\\scene\\living_room\\living_room.obj 1.0 simple_pbr \
#     -inst scene \
#     -name rectlight0 -emittance 1200 0 0 -rectangle 0.1 0.1 \
#     -begin-pos -0.2 1.05 2.586 -end-pos -0.2 1.05 3.0 -freq 5 -time 1.0 -inst rectlight0 \
# "


cmd(c)

with open(f"./exp/{exp_name}/cmd.txt", "w") as f:
    f.write(c)


# log_parse(exp_path=f'C:\\nonsys\\workspace\\GfxExp\\exp\\{exp_name}', gt_dir='C:/nonsys/workspace/GfxExp/exp/bedroom/full_bias/denoised_imgs')
# log_parse(exp_path=f'C:\\nonsys\\workspace\\GfxExp\\exp\\pt_res_64_avg')



