#!/bin/bash

# 检查参数数量
if [ "$#" -ne 2 ]; then
    echo "Usage: \$0 <pattern> <log_files>"
    exit 1
fi

# 获取传入的参数
pattern="$1"
log_files="$2"

# 遍历所有日志文件
for file in $log_files; do
    # 检查文件是否存在
    if [ -f "$file" ]; then
        # 获取文件名
        filename=$(basename "$file")

        # 使用 grep 和 tail 获取最后一行匹配的行
        last_line=$(grep "$pattern" "$file" | tail -n 1)

        # 输出结果
        if [ -n "$last_line" ]; then
            echo "Last matching line: $last_line"
            echo "---------------------------"
        else
            echo "No matching lines found."
            echo "---------------------------"
        fi
    else
        echo "File: $file does not exist."
        echo "---------------------------"
    fi
done

