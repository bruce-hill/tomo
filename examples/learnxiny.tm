# Tomo is a statically typed, garbage collected imperative language with
# emphasis on simplicity, safety, and speed. Tomo code cross compiles to C,
# which is compiled to a binary using your C compiler of choice.

# To begin with, let's define a main function:
func main()
    # This function's code will run if you run this file.

    # Print to the console
    say("Hello world!")

    # Declare a variable with ':=' (the type is inferred to be integer)
    my_variable := 123

    # Assign a new value
    my_variable = 99

    # Floating point numbers are similar, but require a decimal point:
    my_num := 2.0

    # Strings can use interpolation with the dollar sign $:
    say("My variable is $my_variable and this is a sum: $(1 + 2)")

    say("
        Multiline strings begin with a " at the end of a line and continue in
        an indented region below.
          You can have leading spaces after the first line
          and they'll be preserved.

        The multiline string won't include a leading or trailing newline.
    ")

    # You can log values for debugging with ">>", which will print the line's
    # source code and the value (with syntax highlighting) to the console on
    # stderr.
    >> 1 + 2

    # For assertions, you can use `assert`:
    assert 2 + 3 == 5

    # Assert takes an optional message string, but either way, assertion
    # failures will print a lot of contextual information.
    assert 2 + 3 == 5, "Math is broken"

    # Booleans use "yes" and "no" instead of "true" and "false"
    my_bool := yes

    # Conditionals:
    if my_bool
        say("It worked!")
    else if my_variable == 99
        say("else if")
    else
        say("else")

    # Lists:
    my_numbers := [10, 20, 30]

    # Empty lists require specifying the type:
    empty_list : [Int]
    assert empty_list.length == 0

    # Lists are 1-indexed, so the first element is at index 1:
    assert my_numbers[1] == 10

    # Negative indices can be used to get items from the back of the list:
    assert my_numbers[-1] == 30

    # If an invalid index outside the list's bounds is used (e.g.
    # my_numbers[999]), an error message will be printed and the program will
    # exit.

    # Iteration:
    for num in my_numbers
        >> num

    # Optionally, you can use an iteration index as well:
    for index, num in my_numbers
        pass # Pass means "do nothing"

    # Lists can be created with list comprehensions, which are loops:
    assert [x*10 for x in my_numbers] == [100, 200, 300]
    assert [x*10 for x in my_numbers if x != 20] == [100, 300]

    # Loop control flow uses "skip"/"continue" and "stop"/"break"
    for x in my_numbers
        for y in my_numbers
            if x == y
                skip
                continue # This is the same as `skip`

            # For readability, you can also use postfix conditionals:
            skip if x == y

            if x + y == 60
                # Skip or stop can specify a loop variable if you want to
                # affect an enclosing loop:
                stop x
                break x # This is the same as `stop x`

    # Tables are efficient hash maps
    table := {"one"=1, "two"=2}
    assert table["two"] == 2

    # The value returned is optional because none will be returned if the key
    # is not in the table:
    assert table["xxx"] == none

    # Optional values can be converted to regular values using `!` (which will
    # create a runtime error if the value is null):
    assert table["two"]! == 2

    # You can also use `or` to provide a fallback value to replace none:
    assert table["xxx"] or 0 == 0

    # Empty tables require specifying the key and value types:
    empty_table : {Text:Int}

    # Tables can be iterated over either by key or key,value:
    for key in table
        pass

    for key, value in table
        pass

    # Tables also have ".keys" and ".values" fields to explicitly access the
    # list of keys or values in the table.
    assert table.keys == ["one", "two"]
    assert table.values == [1, 2]

    # Tables can have a fallback table that's used as a fallback when the key
    # isn't found in the table itself:
    table2 := {"three"=3; fallback=table}
    assert table2["two"]! == 2
    assert table2["three"]! == 3

    # Tables can also be created with comprehension loops:
    assert {x=10*x for x in 5} == {1=10, 2=20, 3=30, 4=40, 5=50}

    # If no default is provided and a missing key is looked up, the program
    # will print an error message and halt.

    # Any types can be used in tables, for example, a table mapping lists to
    # strings:
    table3 := {[10, 20]="one", [30, 40, 50]="two"}
    assert table3[[10, 20]]! == "one"

    # So far, the datastructures that have been discussed are all *immutable*,
    # meaning you can't add, remove, or change their contents. If you want to
    # have mutable data, you need to allocate an area of memory which can hold
    # different values using the "@" operator (think: "(a)llocate").
    my_arr := @[10, 20, 30]
    my_arr[1] = 999
    assert my_arr[] == [999, 20, 30]

    # To call a method, you must use ":" and the name of the method:
    my_arr.sort()
    assert my_arr[] == [20, 30, 999]

    # To access the immutable value that resides inside the memory area, you
    # can use the "[]" operator:
    assert my_arr[] == [20, 30, 999]

    # You can think of this like taking a photograph of what's at that memory
    # location. Later, a new value might end up there, but the photograph will
    # remain unchanged.
    snapshot := my_arr[]
    my_arr.insert(1000)
    assert my_arr[] == [20, 30, 999, 1000]
    assert snapshot == [20, 30, 999]
    # Internally, this is implemented using copy-on-write, so it's quite
    # efficient.

    # These demos are defined below:
    demo_keyword_args()
    demo_structs()
    demo_enums()
    demo_lambdas()

# Functions must be declared at the top level of a file and must specify the
# types of all of their arguments and return value (if any):
func add(x:Int, y:Int -> Int)
    return x + y

# Default values for arguments can be provided in place of a type (the type is
# inferred from the default value):
func show_both(first:Int, second=0 -> Text)
    return "first=$first second=$second"

func demo_keyword_args()
    assert show_both(1, 2) == "first=1 second=2"

    # If unspecified, the default argument is used:
    assert show_both(1) == "first=1 second=0"

    # Arguments can be specified by name, in any order:
    assert show_both(second=20, 10) == "first=10 second=20"

# Here are some different type signatures:
func takes_many_types(
    boolean:Bool,
    integer:Int,
    floating_point_number:Num,
    text_aka_string:Text,
    list_of_ints:[Int],
    table_of_text_to_bools:{Text=Bool},
    pointer_to_mutable_list_of_ints:@[Int],
    optional_int:Int?,
    function_from_int_to_text:func(x:Int -> Text),
)
    pass

# Now let's define our own datastructure, a humble struct:
struct Person(name:Text, age:Int)
    # We can define constants here if we want to:
    max_age := 122

    # Methods are defined here as well:
    func say_age(self:Person)
        say("My age is $self.age")

    # If you want to mutate a value, you must have a mutable pointer:
    func increase_age(self:@Person, amount=1)
        self.age += amount

    # Methods don't have to take a Person as their first argument:
    func get_cool_name(->Text)
        return "Blade"

func demo_structs()
    # Creating a struct:
    alice := Person("Alice", 30)
    assert alice == Person(name="Alice", age=30)

    # Accessing fields:
    assert alice.age == 30

    # Calling methods:
    alice.say_age()

    # You can call static methods by using the class name and ".":
    assert Person.get_cool_name() == "Blade"

    # Comparisons, conversion to text, and hashing are all handled
    # automatically when you create a struct:
    bob := Person("Bob", 30)
    assert alice == bob == no

    assert "$alice" == 'Person(name="Alice", age=30)' == yes

    table := {alice="first", bob="second"}
    assert table[alice]! == "first"


# Now let's look at another feature: enums. Tomo enums are tagged unions, also
# known as "sum types". You enumerate all the different types of values
# something could have, and it's stored internally as a small integer that
# indicates which type it is, and any data you want to associate with it.
enum Shape(
    Point,
    Circle(radius:Num),
    Rectangle(width:Num, height:Num),
)
    # Just like with structs, you define methods and constants inside a level
    # of indentation:
    func get_area(self:Shape->Num)
        # In order to work with an enum, it's most often handy to use a 'when'
        # statement to get the internal values:
        when self is Point
            return 0
        is Circle(r)
            return Num.PI * r^2
        is Rectangle(w, h)
            return w * h
        # 'when' statements are checked for exhaustiveness, so the compiler
        # will give an error if you forgot any cases. You can also use 'else:'
        # if you want a fallback to handle other cases.

func demo_enums()
    # Enums are constructed like this:
    my_shape := Shape.Circle(1.0)

    # If an enum type doesn't have any associated data, it is not invoked as a
    # function, but is just a static value:
    other_shape := Shape.Point

    # Similar to structs, enums automatically define comparisons, conversion
    # to text, and hashing:
    assert my_shape == other_shape == no

    assert "$my_shape" == "Circle(1)" == yes

    assert {my_shape: "nice"} == {Shape.Circle(1): "nice"}

func demo_lambdas()
    # Lambdas, or anonymous functions, can be used like this:
    add_one := func(x:Int) x + 1
    assert add_one(5) == 6

    # Lambdas can capture closure values, but only as a snapshot from when the
    # lambda was created:
    n := 10
    add_n := func(x:Int) x + n
    assert add_n(5) == 15

    # The lambda's closure won't change when this variable is reassigned:
    n = -999
    assert add_n(5) == 15

