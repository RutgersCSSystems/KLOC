#!/bin/bash
echo "Configure scripts to repo"
git config credential.helper store
git config --global user.name "sudarsun"
git config --global core.editor vim
echo "a" > test.txt
git add test.txt
git commit -am "test"
git rm test.txt
git push origin master
