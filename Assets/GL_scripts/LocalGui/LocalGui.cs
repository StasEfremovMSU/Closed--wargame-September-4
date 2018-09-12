using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;

public class LocalGui : MonoBehaviour {

    bool menu = false;

    public GameObject RebuildMenu;

    public Button Line;
    public Button Avalanche;
    public Button Column;
    public Button Wedge;
    public Button Quads;
    public Button Loose_structure;

    public Button Left1;
    public Button Left2;
    public Button Center1;
    public Button Center2;
    public Button Right1;
    public Button Right2;

    public Button OK;

    void Start()
    {
        RebuildMenu.SetActive(false);
        Line.onClick.AddListener(delegate { BLine(); });
        Line.onClick.AddListener(delegate { BAvalanche(); });
        Line.onClick.AddListener(delegate { BColumn(); });
        Line.onClick.AddListener(delegate { BWedge(); });
        Line.onClick.AddListener(delegate { BQuads(); });
        Line.onClick.AddListener(delegate { BLoose_structure(); });

        Line.onClick.AddListener(delegate { BLeft1(); });
        Line.onClick.AddListener(delegate { BLeft2(); });
        Line.onClick.AddListener(delegate { BCenter1(); });
        Line.onClick.AddListener(delegate { BCenter2(); });
        Line.onClick.AddListener(delegate { BRight1(); });
        Line.onClick.AddListener(delegate { BRight2(); });


        Line.onClick.AddListener(delegate { BOK(); });
    }

    void BLine()
    {
        mainStatic.regiment_stat[GameStatus.RegControl].countTypes();
        mainStatic.regiment_stat[GameStatus.RegControl].rebuilding(1);
        mainStatic.updateRegimentPosition(GameStatus.RegControl);
    }

    void BAvalanche()
    {
        mainStatic.regiment_stat[0].countTypes();
        mainStatic.regiment_stat[0].rebuilding(1);
        mainStatic.updateRegimentPosition(0);
    }

    void BColumn()
    {
        mainStatic.regiment_stat[0].countTypes();
        mainStatic.regiment_stat[0].rebuilding(1);
        mainStatic.updateRegimentPosition(0);
    }

    void BWedge()
    {
        mainStatic.regiment_stat[GameStatus.RegControl].countTypes();
        mainStatic.regiment_stat[GameStatus.RegControl].rebuilding(2);
        mainStatic.updateRegimentPosition(GameStatus.RegControl);
    }

    void BQuads()
    {
        mainStatic.regiment_stat[0].countTypes();
        mainStatic.regiment_stat[0].rebuilding(1);
        mainStatic.updateRegimentPosition(0);
    }

    void BLoose_structure()
    {
        mainStatic.regiment_stat[0].countTypes();
        mainStatic.regiment_stat[0].rebuilding(1);
        mainStatic.updateRegimentPosition(0);
    }

    void BLeft1()
    {
        mainStatic.regiment_stat[GameStatus.RegControl].CommanderTypePosition = 0;
    }

    void BLeft2()
    {
        mainStatic.regiment_stat[GameStatus.RegControl].CommanderTypePosition = 3;
    }

    void BCenter1()
    {
        mainStatic.regiment_stat[GameStatus.RegControl].CommanderTypePosition = 1;
    }

    void BCenter2()
    {
        mainStatic.regiment_stat[GameStatus.RegControl].CommanderTypePosition = 4;
    }

    void BRight1()
    {
        mainStatic.regiment_stat[GameStatus.RegControl].CommanderTypePosition = 2;
    }

    void BRight2()
    {
        mainStatic.regiment_stat[GameStatus.RegControl].CommanderTypePosition = 5;
    }

    void BOK()
    {
        mainStatic.regiment_stat[GameStatus.RegControl].countTypes();
        mainStatic.updateRegimentPosition(GameStatus.RegControl);
    }
	
	// Update is called once per frame
	void Update () {

        if (Input.GetKeyDown(KeyCode.R))
        {
            menu = !menu;
            if (menu == true)
            {
                ///text.text = "Режим управления полком";
                RebuildMenu.SetActive(true);
                Time.timeScale = 0;
            }
            else
            {
                RebuildMenu.SetActive(false);
                Time.timeScale = 1;
                Debug.Log("ASD!!!!F");
            }
        }

        if (Input.GetKeyDown(KeyCode.E))
        {
            RebuildMenu.SetActive(false);
            Time.timeScale = 1;
        }

	}


    public void ClikeSlots(Button but)
    {

    }
}
