A minidump structure definition for Okteta. Allows navigating the
structure of a minidump in a hex editor.

To use, first install Okteta. Then inform Okteta about this structure definition
```
mkdir -p ~/.local/share/okteta/structures/
ln -s $PWD ~/.local/share/okteta/structures/
```
where $PWD ends with `/okteta-minidump`.

Show the Structure View in Okteta with Tools > Structures.  Open the Structure
Settings dialog from the "Settings" in the lower right of the Structures View.
Select the "Structures management" section.  Enable "Minidump files" and
"Apply".  In the "Value Display" section set "Usigned values:" to "Hexadecimal".
Open a minidump file.
