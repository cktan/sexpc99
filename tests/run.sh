function fail {
    echo '[fail]'
}

function ok {
    echo '[ok]'
}


for i in 1 2 3 4; do
    echo -n sample$i '  '
    ../pprint < sample$i > sample$i.out && diff sample$i.{res,out} && ok || fail
done
