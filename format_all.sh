#!/bin/bash

for file in vulkan/*.{cpp,hpp,h} vulkan/external/*.{cpp,hpp} *.{hpp,cpp}
do
    echo "Formatting file: $file ..."
    clang-format -style=file -i $file
done
