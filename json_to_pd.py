import numpy as np
import json
from pathlib import Path
import pandas as pd
import argparse

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--exp_path', type=str, default='C:/nonsys/workspace/GfxExp/exp/living_room_diffuse/save_query_spp_16')
    parser.add_argument('--del_json', action='store_true', default=False)
    args = parser.parse_args()
    exp_path = Path(args.exp_path)
    
    json_lists = sorted(list(exp_path.glob('*.json')))

    N_FRAME = len(json_lists)

    print(f'found {N_FRAME} frames!')

    for frame_id in range(N_FRAME):
        with (json_lists[frame_id]).open('r') as f:
            json_dict = json.load(f)

        # df_pre_train_infer = pd.json_normalize(json_dict['pre_train_infer'])
        df_train_query = pd.json_normalize(json_dict['train_query'])
        # df_train_vertex = pd.json_normalize(json_dict['train_vertex'])
        # df_rendering_infer = pd.json_normalize(json_dict['rendering_infer'])

        for df in [df_train_query]:
            for col in df.columns:
                if isinstance(df[col][0], list):
                    # print(col)
                    df[[f'{col}_x', f'{col}_y', f'{col}_z']] = df[col].tolist()
                    df.drop(col,axis=1, inplace=True)

            for col in df.columns:
                df[col] = df[col].astype(np.float16)

        

        out_path = exp_path / f'{json_lists[frame_id].stem}.h5'
        if out_path.exists():
            out_path.unlink()

        store = pd.HDFStore(str(out_path))
        # store['df_pre_train_infer'] = df_pre_train_infer
        store['df_train_query'] = df_train_query
        # store['df_train_vertex'] = df_train_vertex
        # store['df_rendering_infer'] = df_rendering_infer

        print(f"saved: {str(out_path)}")
        store.close()

        if args.del_json:
            json_lists[frame_id].unlink()
        

                        


if __name__ == '__main__':
    main()