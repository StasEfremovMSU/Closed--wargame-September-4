using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;

public class GameStatus : MonoBehaviour {

    // Режим выбора
    public Camera main_camera;

    public static bool fr, bk, fl, fw, bw, bl;

    public static Vector3 Cam1;
    public static Quaternion Cam2;

    public static bool statesChoosen;

    /// <summary>
    /// Режим перестроения полка
    /// </summary>
    public static bool statesRegiment;

    public static bool UnitControl;

    public static bool ControlReset;

    public static int RegControl;

    public static string listItem;

    public Canvas canvac;

    public Text text;
    public Text text1;
    public Text text2;

    public Button TypeF1;
    public Button TypeF2;
    public Button TypeF3;
    public Button TypeF4;
    public Button TypeF5;
    public Button TypeF6;
    public Button TypeO1;
    public Button TypeO2;
    public Button TypeO3;
    public Button TypeO4;
    public Button TypeO5;
    public Button TypeO6;

    public Text InputL1;
    public Text InputL2;
    public InputField L;
    public InputField R;
    public GameObject REBUILD;

    GameStatus()
    {
        statesRegiment = false;
        statesChoosen = false;
        UnitControl = false;
        ControlReset = false;
        fr = bk = fl = fw = bw =  bl = false;
        
        
    }


    void Update ()
    {
        
        if (Input.GetKeyDown(KeyCode.C))
        {
            statesChoosen = !statesChoosen;
            if (statesChoosen == true)
            {
                text.text = "Режим перехода к юниту";
            }
            if (statesChoosen == false)
            {
                text.text = "";
            } 
        }

        if (Input.GetKeyDown(KeyCode.V))
        {
            if (UnitControl == true)
            {
                ControlReset = true;
                text.text = "Выход из управления юнитом";
            }
        }


        if (fr == false && fl == false && fr == false && bk == false && bl == false &&  bw ==false )
        {
              text1.text = "!";
        }
        
        if (fr == true)
        {
            text1.text = "Впереди";
            fr = false;
        }
        if (bk == true)
        {
            text1.text = "Сзади";
            bk = false;
        }
        if (fw == true)
        {
            text1.text = "Впереди-справо";
            fw = false;
        }
        if (fl == true)
        {
            text1.text = "Впереди-слево";
            fl = false;
        }
        if (bw == true)
        {
            text1.text = "Сзади-справа";
            bw = false;
        }
        if (bl == true)
        {
            text1.text = "Сзади-слева";
            bl = false;
        }

        text2.text = listItem;


    }

	
}
