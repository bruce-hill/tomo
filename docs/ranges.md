# Ranges

Ranges are Tomo's way to do iteration over numeric ranges. Ranges are typically
created using the `Int.to()` method like so: `5:to(10)`. Ranges are
*inclusive*.

```tomo
>> [i for i in 3:to(5)]
= [3, 4, 5]
```

---

## Range Methods

### `reversed`

**Description:**  
Returns a reversed copy of the range.

**Usage:**  
```tomo
reversed(range: Range) -> Range
```

**Parameters:**

- `range`: The range to be reversed.

**Returns:**  
A new `Range` with the order of elements reversed.

**Example:**  
```tomo
>> 1:to(5):reversed()
= Range(first=5, last=1, step=-1)
```

---

### `by`

**Description:**  
Creates a new range with a specified step value.

**Usage:**  
```tomo
by(range: Range, step: Int) -> Range
```

**Parameters:**

- `range`: The original range.
- `step`: The step value to be used in the new range.

**Returns:**  
A new `Range` that increments by the specified step value.

**Example:**  
```tomo
>> 1:to(5):by(2)
= Range(first=1, last=5, step=2)
```

---
