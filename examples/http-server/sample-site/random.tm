#!/bin/env tomo
use random

func main()
    say("
        <!DOCTYPE HTML>
        <html>
        <head>
        <title>Random Number</title>
        <link rel="stylesheet" href="styles.css">
        </head>
        <body>
        <h1>Random Number</h1>
        Your random number is: $(random.int(1,100))
        </body>
        </html>
    ")
