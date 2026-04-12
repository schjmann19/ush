if true; then echo ok; fi
if false; then echo bad; elif true; then echo elif-ok; fi
if false; then echo bad; elif false; then echo bad2; elif true; then echo elif2; else echo else-ok; fi
