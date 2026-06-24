DualCare Android module — README

Quick start
1. Open android-app in Android Studio. Sync Gradle.
2. Ensure Android SDK & Kotlin plugin installed.
3. Firebase: project ID dualcare2-88208. google-services.json is in app/.
4. Enable Email/Password auth and create users (salmaaboelelaa8@gmail.com, sandy.joseph2006@gmail.com).
5. Deploy DB rules: firebase login; firebase use --add dualcare2-88208; firebase deploy --only database
6. Run on a device (grant BODY_SENSORS, INTERNET). App streams accelerometer to /sensors/accelerometer.

Notes
- Use Firebase CLI to manage rules and deploy. Commit changes locally and push from your machine.