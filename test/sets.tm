
func main()
    >> t1 := @|10, 20, 30, 10|
    = @|10, 20, 30|
    >> t1.has(10)
    = yes
    >> t1.has(-999)
    = no

    >> t2 := |30, 40|

    >> t1.with(t2)
    >> |10, 20, 30, 40|

    >> t1.without(t2)
    >> |10, 20|

    >> t1.overlap(t2)
    >> |30|


    >> |1,2|.is_subset_of(|2,3|)
    = no
    >> |1,2|.is_subset_of(|1,2,3|)
    = yes
    >> |1,2|.is_subset_of(|1,2|)
    = yes
    >> |1,2|.is_subset_of(|1,2|, strict=yes)
    = no

    >> t1.add_all(t2)
    >> t1
    = @|10, 20, 30, 40|
    >> t1.remove_all(t2)
    >> t1
    = @|10, 20|

    >> |3, i for i in 5|
    = |3, 1, 2, 4, 5|

    >> empty : |Int| = ||
    = ||
