// `gea build` collects `<package>/geatsc-plugin` from every dependency of an
// app, so depending on @geastack/windows is what installs the Windows-native
// compiler plugin. The plugin itself lives in its own package.
export { default } from '@geastack/geatsc-plugin-windows-native'
