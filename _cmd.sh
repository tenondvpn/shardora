cd /root/chainbaas/dist
F=assets/index-sL1MTNv0.js
C=assets/index-Bak8kQD1.css
echo "--- md5 ---"
md5sum $F $C
echo "--- index.html refs ---"
grep -o -e 'index-[A-Za-z0-9_.-]*\.js' -e 'index-[A-Za-z0-9_.-]*\.css' index.html
echo "--- new layout class (want 1) ---"
grep -c -e 'tree-search-row' $C
echo "--- old class gone (want 0) ---"
grep -c -e 'tree-actions' $C
echo "--- tooltips present (want >=1 each) ---"
grep -o -e '新建合约' $F | wc -l
grep -o -e '转账' $F | wc -l
echo "--- inline labels NOT in button text (buttons are icon-only) ---"
grep -o -e '.{0,40}DocumentAdd.{0,60}' $F | head -2
