plugins {
    id("net.fabricmc.fabric-loom-remap") version "1.16.2"
    `java-library`
}

// Properties read from gradle.properties (snake_case there → camelCase here).
// `by project` would only match if the names matched verbatim, which they don't.
val minecraftVersion = property("minecraft_version") as String
val yarnMappings    = property("yarn_mappings")    as String
val loaderVersion   = property("loader_version")   as String
val fabricVersion   = property("fabric_version")   as String
val modVersion      = property("mod_version")      as String
val mavenGroup      = property("maven_group")      as String
val archivesBaseName = property("archives_base_name") as String
val lwjglVersion    = property("lwjgl_version")    as String

version = modVersion
group = mavenGroup
base.archivesName.set(archivesBaseName)

// Note: no `toolchain { languageVersion ... }`. With a pinned toolchain
// Gradle insists on locating that exact JDK and won't auto-download without
// the foojay resolver (which doesn't always cooperate). Instead we compile
// with whatever JDK is current and force --release 21 bytecode via
// JavaCompile.options.release below. CI uses setup-java to pin JDK 21
// explicitly. MC at runtime needs >= 21.
java {
    sourceCompatibility = JavaVersion.VERSION_21
    targetCompatibility = JavaVersion.VERSION_21
    withSourcesJar()
}

repositories {
    maven("https://maven.fabricmc.net/")
    mavenCentral()
}

dependencies {
    minecraft("com.mojang:minecraft:$minecraftVersion")
    mappings("net.fabricmc:yarn:$yarnMappings:v2")
    modImplementation("net.fabricmc:fabric-loader:$loaderVersion")
    modImplementation("net.fabricmc.fabric-api:fabric-api:$fabricVersion")

    // LWJGL Vulkan bindings — MC already pulls LWJGL core, but vulkan module
    // is not a default. We use it only for surface creation in Java; the hot
    // path lives in the native lib.
    implementation("org.lwjgl:lwjgl-vulkan:$lwjglVersion")
}

loom {
    accessWidenerPath.set(file("src/main/resources/rtxmc.accesswidener"))
    runs {
        named("client") {
            vmArgs("-Dorg.lwjgl.util.Debug=true")
            // Streamline interposer needs to find sl.* plugin DLLs.
            vmArgs("-Djava.library.path=${projectDir}/native/build/Release;${projectDir}/streamline/bin/x64")
            // Same JDK 25 + Cloudflare cacerts issue we hit in settings.gradle.kts —
            // MC's auth-lib hits sessionserver.mojang.com via Cloudflare too.
            if (System.getProperty("os.name").startsWith("Windows")) {
                vmArgs("-Djavax.net.ssl.trustStoreType=Windows-ROOT")
            }
        }
    }
}

tasks.withType<JavaCompile>().configureEach {
    options.release.set(21)
    options.encoding = "UTF-8"
}

tasks.processResources {
    inputs.property("version", project.version)
    inputs.property("mc_version", minecraftVersion)
    filesMatching("fabric.mod.json") {
        expand("version" to project.version, "mc_version" to minecraftVersion)
    }
}

// Build the native lib only when explicitly requested. CI builds them in
// separate jobs to avoid needing the Vulkan SDK on the mod-jar runner.
val withNative = (findProperty("withNative") as String?)?.toBoolean() ?: false

tasks.register<Exec>("buildNative") {
    workingDir = file("native")
    commandLine("cmake", "--build", "build", "--config", "Release")
}

if (withNative) {
    tasks.named("assemble") { dependsOn("buildNative") }
}
