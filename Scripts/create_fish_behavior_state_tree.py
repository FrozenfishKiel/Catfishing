"""Create/update ST_FishFight through the persistent CatfishingEditor authoring utility.

Run from Unreal Editor Python console:
    py "D:/develop/Catfishing/Scripts/create_fish_behavior_state_tree.py"
"""

import unreal


def main():
    if not unreal.CatFishStateTreeAuthoringLibrary.create_or_update_default_fish_behavior_state_tree():
        raise RuntimeError("Could not create /Game/Data/StateTrees/ST_FishFight")
    unreal.log("Catfishing fish behavior StateTree created and compiled.")


if __name__ == "__main__":
    main()
