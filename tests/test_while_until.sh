while false; do
  echo "ERROR: while body should not execute"
done

until true; do
  echo "ERROR: until body should not execute"
done

while false
 do
  echo "ERROR: multiline while body should not execute"
done

until true
 do
  echo "ERROR: multiline until body should not execute"
done
