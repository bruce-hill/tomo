-- Convert code to bold
function Code(el)
    return pandoc.Strong(el.text)
end

-- Convert code blocks to bold and indented
function CodeBlock(el)
    return pandoc.BlockQuote({pandoc.Para(pandoc.Strong(el.text))})
end
