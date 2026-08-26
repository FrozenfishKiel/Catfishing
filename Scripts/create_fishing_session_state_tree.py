"""Rebuild the authoritative fishing-session StateTree from the checked-in C++ topology."""

import unreal


def main():
    if not unreal.CatFishStateTreeAuthoringLibrary.create_or_update_default_fishing_session_state_tree():
        raise RuntimeError("Could not create /Game/Data/StateTrees/ST_FishingSession")
    unreal.log("Catfishing fishing-session StateTree created and compiled.")


if __name__ == "__main__":
    main()
