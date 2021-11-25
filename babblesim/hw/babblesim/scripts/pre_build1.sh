#!/bin/bash


if [ -z ${BSIM_COMPONENTS_PATH+x} ]; then
  echo "This board requires the BabbleSim simulator. Please set" \
        "the environment variable BSIM_COMPONENTS_PATH to point to its components" \
        "folder. More information can be found in" \
        "https://babblesim.github.io/folder_structure_and_env.html"
  exit 1
fi

if [ -z ${BSIM_OUT_PATH+x} ]; then
  echo "This board requires the BabbleSim simulator. Please set" \
        "the environment variable BSIM_OUT_PATH to point to the folder where the" \
        "simulator is compiled to. More information can be found in" \
        "https://babblesim.github.io/folder_structure_and_env.html"
  exit 1
fi

if [[ -d "$(pwd)/components" ]]
then
    echo "Babblesim components: Directory exists. Removing and linking again..."
    rm -r $(pwd)/components
else
    echo "Babblesim components: Linking components..."
fi

ln -nsf $BSIM_OUT_PATH/components

if [[ -d "$(pwd)/src" ]]
then
    echo "Babblesim libraries src: Directory exists. Removing and linking again..."
    rm -r $(pwd)/src
else
    echo "Babblesim components: Linking components..."
fi

mkdir -p src
ln -nsf $BSIM_OUT_PATH/components
#ln -nsf $BSIM_OUT_PATH/lib

# Create links to all .32.a files
find $BSIM_OUT_PATH/lib/$lib -name "*.32.a" -type f -exec ln -t src -nsf {} \;
#find $BSIM_OUT_PATH/lib/$lib -name "*.so" -type f -exec ln -t src -nsf {} \;

# Create links just to selected .32.a files
#lib_list="libUtilv1.32.a libPhyComv1.32.a lib2G4PhyComv1.32.a libRandv2.32.a"
#for lib in $lib_list; do
#    ln -t src -nsf $BSIM_OUT_PATH/lib/$lib
#done
