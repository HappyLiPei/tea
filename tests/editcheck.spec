database: edit_tmp.db
id: id

input {
    input file: editcheck.data
    output table: ed
    overwrite: yes
}

fields{
    y1: int 1, 2, 3, 4
    y2: int 1, 2, 3, 4
    weights: real
}

checks {
    #ordered
    y2+0.0 < y1 +0.0 # => y1=y2, y2=y1

    y1=3 and y2=2

    #sum is odd
    (y1+y2) % 2 = 1

    y1=1

    y2*weights > 100

    #sum is even
    weights/y1 > 30

}