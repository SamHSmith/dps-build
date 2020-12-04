#!/bin/sh

make -C sbase sbase-box

sbase/sbase-box | sed 's/\[ //g' | sed 's/ /\n/g' | sed -e '${/^$/d}' | awk '{printf "sbase/sbase-box:/bin/"; print $1}' > register


