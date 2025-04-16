#!/usr/bin/awk -f

# Initialize variables from command-line arguments
BEGIN {
    #if (ARGC != 6) {
    #    print "Usage: awk -f generate_array.awk -v CLASS_NAME=<name> -v CLASS_NAME_UPPER=<upper> -v CLASS_NAME_LOWER=<lower> -v ELEMENT_TYPE=<type> -v PREFIX=<prefix> input.in > output"
    #    exit 1
    #}
    class_name = CLASS_NAME
    class_name_upper = CLASS_NAME_UPPER
    class_name_lower = CLASS_NAME_LOWER
    element_type = ELEMENT_TYPE
    prefix = PREFIX
}

# Substitute placeholders with provided values
{
    gsub(/CLASS_NAME_UPPER/, class_name_upper)
    gsub(/CLASS_NAME_LOWER/, class_name_lower)
    gsub(/CLASS_NAME/, class_name)
    gsub(/ELEMENT_TYPE/, element_type)
    gsub(/PREFIX/, prefix)
    if (include1 != "") {
        gsub(/\/\* INCLUDE1 \*\//, include1)
    }   
    if (include2 != "") {
        gsub(/\/\* INCLUDE2 \*\//, include2)
    }   
    print
}
