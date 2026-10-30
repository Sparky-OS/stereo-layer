#!/bin/sh
$EXTRACT_TR_STRINGS `find . -name '*.cpp' -o -name '*.h'` -o po/stereo-layer_qt.pot
