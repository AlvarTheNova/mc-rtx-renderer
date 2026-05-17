// JDK 25's bundled cacerts on Windows can't validate Cloudflare-fronted
// repos (maven.fabricmc.net, etc.) — handshake fails with PKIX path
// building error. Routing through the Windows system trust store fixes it
// without requiring keytool surgery on every dev machine. Windows-only;
// Linux/macOS use the JDK default trust store.
if (System.getProperty("os.name").startsWith("Windows")) {
    System.setProperty("javax.net.ssl.trustStoreType", "Windows-ROOT")
}

pluginManagement {
    repositories {
        maven("https://maven.fabricmc.net/") { name = "Fabric" }
        mavenCentral()
        gradlePluginPortal()
    }
}

rootProject.name = "mc-rtx-renderer"
