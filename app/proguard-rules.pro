-keep class os.monkey.shell.Native { *; }
-keepclassmembers class os.monkey.shell.ShellActivity {
    public void launchApp(java.lang.String);
}
