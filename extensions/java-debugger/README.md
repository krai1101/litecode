# Java Debugger

Install a JDK 21 or newer and set JAVA_HOME, add its bin folder to PATH, or set `java.jdt.ls.java.home`. Import this extension folder in LiteCode and enable it. Open the Java project, press F5, then choose Java and Current File or Launch Main Class. For a saved configuration use `type: "java"`, `request: "launch"` and `mainClass` set to the Java source file or fully qualified class name. Attach to Java Process connects to the target's JDWP host and port.

The extension starts [Eclipse JDT LS](https://github.com/eclipse-jdtls/eclipse.jdt.ls/tree/v1.57.0) and loads the [Microsoft Java Debug Server plugin](https://github.com/microsoft/java-debug/tree/0.53.2). The package includes their licenses and notices. The first launch imports the project before building it.
